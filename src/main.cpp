// =====================================================================
//  8888888888     d8888 8888888b.  888b     d888 8888888  .d88888b.
//  888           d88888 888   Y88b 8888b   d8888   888   d88P" "Y88b
//  8888888      d88P888 888   d88P 888Y88888P888   888   888     888
//  888         d88P 888 8888888P"  888 Y888P 888   888   888     888
//  888        d88P  888 888 T88b   888  Y8P  888   888   Y88b. .d88P
//  888       d88P   888 888  T88b  888   "   888 8888888  "Y88888P"
//
//  Vaso inteligente: irriga sozinho, avisa o que esta errado e mostra
//  tudo em tres lugares - display, pagina web e serial.
//
//  ESP32-C3 | DHT22 | umidade de solo | nivel de tanque | ESP32-CAM |
//  OLED SSD1306 | anel WS2812 de 16 pixels | bomba por ponte H mini
//
//  Regra que vale para o arquivo inteiro: NADA BLOQUEIA. Sem delay() em
//  regime, sem while esperando sensor. Um vaso que trava com o Wi-Fi
//  fora do ar e um vaso que deixa a planta secar em silencio.
//
//  Compilar: pio run          Gravar: pio run -t upload
// =====================================================================

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "energia.h"
#include "sensores.h"
#include "camera.h"
#include "telemetria.h"
#include "bomba.h"
#include "anel.h"
#include "tela.h"
#include "web.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Sem secrets.h o firmware COMPILA e roda: sobe o proprio ponto de acesso
// e espera configuracao. E isso que permite o CI compilar sem nenhuma
// senha - e o que impede alguem de commitar credencial por descuido.
#ifndef FARMIO_WIFI_SSID
#define FARMIO_WIFI_SSID ""
#define FARMIO_WIFI_PASS ""
#endif
#ifndef FARMIO_AP_PASS
#define FARMIO_AP_PASS "farmio123"
#endif

// ---- definicao dos globais declarados em config.h -------------------
Leituras L;
Visto V;
EstadoBomba B;
uint8_t riscosAtivos = RISCO_NENHUM;

static uint32_t bootAte = 0;  // fim da animacao de abertura

// ---------------------------------------------------------------------
//  Rede - maquina de estado, nunca um laco de espera
//
//  CAMPO ABERTO: A UNICA INFRAESTRUTURA E O CELULAR. Por isso o vaso
//  fala em duas redes ao mesmo tempo, e cada uma cobre a falha da outra:
//
//    rede propria  'farmio-01', SEMPRE no ar, sempre em 192.168.4.1. O
//                  celular entra nela como em qualquer Wi-Fi. Nao depende
//                  de nada: nem do roteador do celular estar ligado, nem
//                  de alguem descobrir IP. E o caminho que funciona sempre.
//
//    roteador      o roteador do celular (secrets.h). Quando ele esta
//                  ligado e ao alcance, o vaso entra nele tambem, e o
//                  celular alcanca o vaso sem sair da propria rede - e sem
//                  perder os dados moveis. O endereco ai e dado pelo
//                  celular, e aparece na serial e em farmio-01.local.
//
//  Por que a rede propria nao e so "reserva quando o roteador falha": em
//  campo o roteador do celular fica DESLIGADO quase o tempo todo - so
//  existe quando alguem esta ali. Uma rede de reserva que precisasse
//  detectar a falha para subir estaria subindo o tempo inteiro.
//
//  A CONVIVENCIA DAS DUAS TEM UM CUSTO: para procurar o roteador, o radio
//  sai do canal da rede propria por ~2 s, e quem esta conectado nela
//  perde a pagina nesse intervalo. Entao, enquanto houver alguem
//  conectado na rede propria, o vaso NAO procura o roteador. Quem esta
//  usando o vaso tem prioridade sobre quem talvez apareca.
// ---------------------------------------------------------------------
namespace Rede {

static bool online          = false;
static bool temRoteador     = false;
static uint32_t tentativaEm = 0;
static uint32_t esperaMs    = 2000;

inline void begin() {
  temRoteador = strlen(FARMIO_WIFI_SSID) > 0;

  WiFi.persistent(false);
  WiFi.mode(temRoteador ? WIFI_AP_STA : WIFI_AP);
  WiFi.setHostname(FARMIO_NOME);

  WiFi.softAP(FARMIO_NOME, FARMIO_AP_PASS);
  Serial.printf("[rede] rede propria '%s' no ar em http://%s\n", FARMIO_NOME,
                WiFi.softAPIP().toString().c_str());

  if (temRoteador) {
    WiFi.setAutoReconnect(false);  // a reconexao e nossa, com espera crescente
    WiFi.begin(FARMIO_WIFI_SSID, FARMIO_WIFI_PASS);
    tentativaEm = millis();
    Serial.printf("[rede] procurando o roteador '%s'...\n", FARMIO_WIFI_SSID);
  } else {
    Serial.println("[rede] sem secrets.h: so a rede propria");
  }

  // farmio-01.local. Resolve em computador e em parte dos celulares; nos
  // que nao resolvem, o IP sai na serial. Custa pouco e as vezes poupa
  // a procura pelo endereco.
  if (MDNS.begin(FARMIO_NOME)) MDNS.addService("http", "tcp", 80);
}

inline void tick() {
  if (!temRoteador) return;

  const uint32_t agora = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!online) {
      online   = true;
      esperaMs = 2000;
      Serial.printf("[rede] no roteador '%s': http://%s  ou  http://%s.local  (%d dBm)\n",
                    FARMIO_WIFI_SSID, WiFi.localIP().toString().c_str(), FARMIO_NOME, WiFi.RSSI());
    }
    return;
  }

  if (online) {
    online = false;
    Serial.println("[rede] roteador do celular sumiu - a rede propria continua no ar");
    tentativaEm = agora;
  }

  // Alguem usando a rede propria: nao sai do canal para procurar.
  if (WiFi.softAPgetStationNum() > 0) return;

  // Espera crescente ate 60 s: roteador desligado nao merece uma tentativa
  // por segundo consumindo corrente a toa.
  if (agora - tentativaEm < esperaMs) return;
  tentativaEm = agora;
  esperaMs    = esperaMs >= 60000UL ? 60000UL : esperaMs * 2;
  WiFi.disconnect();
  WiFi.begin(FARMIO_WIFI_SSID, FARMIO_WIFI_PASS);
}

}  // namespace Rede

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);  // unica espera do firmware: janela para o monitor engatar
  Serial.println();
  Serial.println(F("====================================================="));
  Serial.printf("  FarmIO %s  |  no: %s\n", FARMIO_VERSAO, FARMIO_NOME);
  Serial.printf("  build %s %s\n", __DATE__, __TIME__);
  Serial.println(F("====================================================="));

  // Energia primeiro de tudo: e ela que decide o teto de brilho e se a
  // bomba pode existir. Decidir isso depois de acender o anel seria
  // acender o anel para so entao descobrir que nao cabia.
  Energia::begin();

  memset(&L, 0, sizeof(L));
  memset(&B, 0, sizeof(B));
  L.temperaturaC = NAN;
  L.umidadeArPct = NAN;
  L.soloFaixa    = SOLO_INVALIDO;

  if (PIN_LED_PLACA >= 0) pinMode(PIN_LED_PLACA, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);

  Bomba::begin();  // primeiro de todos: garante bomba desligada no boot
  Anel::begin();
  Sens::begin();
  Camera::begin();

  if (Tela::begin()) {
    Tela::telaAbertura();
    Serial.println("[tela] OLED respondeu no I2C");
  } else {
    Serial.println("[tela] OLED nao respondeu - seguindo sem display");
  }

  Rede::begin();
  Web::begin();

  bootAte = millis() + 3000;  // 3 s de animacao verde antes de operar
  Telemetria::begin();
  Serial.println("[boot] pronto");
}

// ---------------------------------------------------------------------
void loop() {
  const bool ligando = (int32_t)(millis() - bootAte) < 0;

  Sens::tick();
  // A camera entra com 'B.ligada' porque o orcamento de energia nao
  // deixa os dois picos - bomba girando e camera capturando - caberem na
  // mesma porta USB. Ver energia.h.
  Camera::tick(B.ligada);
  riscosAtivos = (uint8_t)(Sens::avaliaRiscos() | Camera::riscos());

  Bomba::tick();
  Rede::tick();
  Web::tick();

  Anel::reflete(riscosAtivos, ligando);
  Anel::ajustaBrilho(V.enlaceOk, B.ligada);
  Anel::tick();

  if (!ligando) Tela::tick(riscosAtivos);

  // LED da placa: aceso enquanto irriga, apagado no resto. E o
  // diagnostico que sobra quando nem display nem rede respondem.
  if (PIN_LED_PLACA >= 0) digitalWrite(PIN_LED_PLACA, B.ligada ? HIGH : LOW);

  Telemetria::tick();
}
