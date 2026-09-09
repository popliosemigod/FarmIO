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
//  ESP32 DevKit V1 | DHT22 | umidade de solo | nivel de tanque |
//  OLED SSD1306 | anel WS2812 de 16 pixels | bomba 12 V via TB6612FNG
//
//  Regra que vale para o arquivo inteiro: NADA BLOQUEIA. Sem delay() em
//  regime, sem while esperando sensor. Um vaso que trava com o Wi-Fi
//  fora do ar e um vaso que deixa a planta secar em silencio.
//
//  Compilar: pio run          Gravar: pio run -t upload
// =====================================================================

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "sensores.h"
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
EstadoBomba B;
uint8_t riscosAtivos = RISCO_NENHUM;

static uint32_t bootAte = 0;  // fim da animacao de abertura

// ---------------------------------------------------------------------
//  Rede - maquina de estado, nunca um laco de espera
// ---------------------------------------------------------------------
namespace Rede {

static bool online          = false;
static bool modoAp          = false;
static uint32_t tentativaEm = 0;
static uint32_t esperaMs    = 2000;

inline void sobeAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(FARMIO_NOME, FARMIO_AP_PASS);
  modoAp = true;
  Serial.printf("[rede] sem credencial. AP '%s' no ar em %s\n", FARMIO_NOME,
                WiFi.softAPIP().toString().c_str());
}

inline void begin() {
  if (strlen(FARMIO_WIFI_SSID) == 0) {
    sobeAp();
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(FARMIO_NOME);
  WiFi.setAutoReconnect(false);  // a reconexao e nossa, com espera crescente
  WiFi.begin(FARMIO_WIFI_SSID, FARMIO_WIFI_PASS);
  tentativaEm = millis();
  Serial.printf("[rede] conectando em '%s'...\n", FARMIO_WIFI_SSID);
}

inline void tick() {
  if (modoAp) return;

  const uint32_t agora = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!online) {
      online   = true;
      esperaMs = 2000;
      Serial.printf("[rede] conectado. http://%s  (%d dBm)\n", WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
    }
    return;
  }

  if (online) {
    online = false;
    Serial.println("[rede] enlace caiu");
    tentativaEm = agora;
  }

  // Espera crescente ate 60 s: rede fora do ar nao merece uma tentativa
  // por segundo consumindo corrente a toa.
  if (agora - tentativaEm < esperaMs) return;
  tentativaEm = agora;
  esperaMs    = esperaMs >= 60000UL ? 60000UL : esperaMs * 2;
  WiFi.disconnect();
  WiFi.begin(FARMIO_WIFI_SSID, FARMIO_WIFI_PASS);
}

}  // namespace Rede

// ---------------------------------------------------------------------
//  Telemetria pela serial - o canal que funciona sem rede nenhuma
// ---------------------------------------------------------------------
static void heartbeatSerial() {
  static uint32_t proximo = 0;
  const uint32_t agora    = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + INTERVALO_SERIAL_MS;

  char json[640];
  Web::jsonSensores(json, sizeof(json));
  Serial.println(json);
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);  // unica espera do firmware: janela para o monitor engatar
  Serial.println();
  Serial.println(F("====================================================="));
  Serial.printf("  FarmIO %s  |  no: %s\n", FARMIO_VERSAO, FARMIO_NOME);
  Serial.printf("  build %s %s\n", __DATE__, __TIME__);
  Serial.println(F("====================================================="));

  memset(&L, 0, sizeof(L));
  memset(&B, 0, sizeof(B));
  L.temperaturaC = NAN;
  L.umidadeArPct = NAN;
  L.soloFaixa    = SOLO_INVALIDO;

  pinMode(PIN_LED_PLACA, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);

  Bomba::begin();  // primeiro de todos: garante bomba desligada no boot
  Anel::begin();
  Sens::begin();

  if (Tela::begin()) {
    Tela::telaAbertura();
    Serial.println("[tela] OLED respondeu no I2C");
  } else {
    Serial.println("[tela] OLED nao respondeu - seguindo sem display");
  }

  Rede::begin();
  Web::begin();

  bootAte = millis() + 3000;  // 3 s de animacao verde antes de operar
  Serial.println("[boot] pronto");
}

// ---------------------------------------------------------------------
void loop() {
  const bool ligando = (int32_t)(millis() - bootAte) < 0;

  Sens::tick();
  riscosAtivos = Sens::avaliaRiscos();

  Bomba::tick();
  Rede::tick();
  Web::tick();

  Anel::reflete(riscosAtivos, ligando);
  Anel::tick();

  if (!ligando) Tela::tick(riscosAtivos);

  // LED da placa: aceso enquanto irriga, apagado no resto. E o
  // diagnostico que sobra quando nem display nem rede respondem.
  digitalWrite(PIN_LED_PLACA, B.ligada ? HIGH : LOW);

  heartbeatSerial();
}
