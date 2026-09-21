// =====================================================================
//  FarmIO - main_ensaio.cpp
//  Bring-up de DOIS sensores e mais nada: o DHT22 (temperatura e umidade
//  do ar) e o sensor de nivel do tanque.
//
//  POR QUE UM FIRMWARE SO PARA ISSO. O firmware do vaso sobe Wi-Fi,
//  ponto de acesso, servidor HTTP, I2C, anel de LED e enlace com a
//  camera antes de imprimir a primeira leitura. Se o DHT22 nao responder
//  ali, a lista de suspeitos tem quinze nomes. Aqui tem tres: o fio, o
//  resistor e o sensor. Isolar e o que torna o resultado interpretavel.
//
//  O QUE ESTE ENSAIO NAO FAZ, de proposito: nao liga radio, nao toca no
//  I2C, nao acende LED e nao fala com a camera. A unica saida que ele
//  aciona e o pino da bomba - forcado a nivel baixo, porque a regra do
//  projeto e que o modo de falha seguro e bomba desligada, e um ensaio
//  nao e desculpa para abrir excecao.
//
//  LIGACAO ESPERADA (ESP32-C3):
//
//      DHT22 ---- dados -> GPIO5, com pull-up de 10 k para 3V3
//              -- VCC   -> 3V3
//              -- GND   -> GND
//
//      Nivel ---- AO    -> GPIO1  (ADC1_CH1)
//              -- VCC   -> 3V3   <-- NUNCA 5 V: o ADC e de 3,3 V
//              -- GND   -> GND
//
//  Gravar e ler:
//      pio run -e ensaio -t upload
//      pio device monitor -e ensaio
// =====================================================================
#include <Arduino.h>
#include <DHT.h>

#include "config.h"

static DHT g_dht(PIN_DHT, DHT22);

// Janela de estatistica do ADC. Um unico numero nao diz nada sobre um
// sensor analogico: e a DISPERSAO que separa "ligado e parado" de "pino
// flutuando". Pino solto em entrada de alta impedancia passeia por
// centenas de contagens; sensor ligado fica quieto dentro de poucas
// dezenas.
static const int JANELA = 40;  // 40 amostras a 100 ms = 4 s de historico
static uint16_t g_amostras[JANELA];
static int g_n    = 0;
static int g_topo = 0;

static uint32_t g_lidasDht = 0, g_falhasDht = 0, g_falhasSeguidas = 0;
static float g_tempC = NAN, g_umid = NAN;

static uint32_t g_proxAdc = 0, g_proxDht = 0, g_proxLinha = 0;

// ---------------------------------------------------------------------
//  Estatistica da janela
// ---------------------------------------------------------------------
struct Resumo {
  uint16_t minimo, maximo, media;
  uint16_t espalhamento;
};

static Resumo resume() {
  Resumo r;
  r.minimo      = 4095;
  r.maximo      = 0;
  uint32_t soma = 0;
  for (int i = 0; i < g_n; i++) {
    const uint16_t v = g_amostras[i];
    if (v < r.minimo) r.minimo = v;
    if (v > r.maximo) r.maximo = v;
    soma += v;
  }
  if (g_n == 0) {
    r.minimo = r.maximo = r.media = r.espalhamento = 0;
    return r;
  }
  r.media        = (uint16_t)(soma / (uint32_t)g_n);
  r.espalhamento = (uint16_t)(r.maximo - r.minimo);
  return r;
}

// Percentual do tanque pelos limiares do config.h. Vale repetir que
// esses limiares sao chute educado ate a calibracao - o numero cru ao
// lado e que serve para calibrar.
static uint8_t tanquePct(uint16_t adc) {
  if (adc <= NIVEL_VAZIO_ADC) return 0;
  if (adc >= NIVEL_CHEIO_ADC) return 100;
  const uint32_t faixa = (uint32_t)(NIVEL_CHEIO_ADC - NIVEL_VAZIO_ADC);
  return (uint8_t)(((uint32_t)(adc - NIVEL_VAZIO_ADC) * 100UL) / faixa);
}

// ---------------------------------------------------------------------
//  Diagnostico em texto. Numero sozinho nao ajuda quem esta com o ferro
//  de solda na mao; o que ajuda e a frase que diz onde olhar.
// ---------------------------------------------------------------------
static const char* diagnosticoNivel(const Resumo& r) {
  if (g_n < JANELA) return "aquecendo";
  if (r.espalhamento > 400) return "INSTAVEL - pino parece flutuando (sensor ligado? GND comum?)";
  if (r.media < 40) return "encostado no zero - AO no GND, ou sensor sem VCC";
  if (r.media > 4050) return "encostado no teto - AO no 3V3, ou sensor alimentado em 5 V";
  return "estavel";
}

static const char* diagnosticoDht() {
  if (g_lidasDht == 0) return "aquecendo";
  if (g_falhasSeguidas >= 3) return "MUDO - cheque pull-up de 10 k, VCC em 3V3 e o proprio fio";
  if (g_falhasDht > 0) return "responde, com falhas";
  return "ok";
}

// ---------------------------------------------------------------------
void setup() {
  // Bomba desligada antes de qualquer outra coisa. Vale tambem no ensaio.
  pinMode(PIN_BOMBA_PWM, OUTPUT);
  digitalWrite(PIN_BOMBA_PWM, LOW);

  Serial.begin(115200);
  // O USB-CDC nativo do C3 so enumera depois que o host abre a porta.
  // Espera limitada: passou de 2,5 s, segue sem console - o ensaio nao
  // pode depender de haver alguem olhando.
  const uint32_t ate = millis() + 2500;
  while (!Serial && (int32_t)(millis() - ate) < 0) {
  }

  Serial.println();
  Serial.println(F("====================================================="));
  Serial.println(F("  FarmIO - ensaio de DOIS sensores"));
  Serial.printf("  DHT22 no GPIO%d   |   nivel no GPIO%d (ADC1)\n", PIN_DHT, PIN_NIVEL);
  Serial.println(F("  sem Wi-Fi, sem I2C, sem LED, sem camera"));
  Serial.println(F("====================================================="));
  Serial.println();

  g_dht.begin();

  // 11 dB abre a faixa do ADC para ~0..3,1 V, que e o que o sensor de
  // nivel alimentado em 3V3 entrega. Na atenuacao padrao (0 dB) tudo
  // acima de ~0,95 V satura em 4095 e a leitura vira uma linha reta.
  analogSetPinAttenuation(PIN_NIVEL, ADC_11db);
  analogReadResolution(12);

  Serial.println(F("tempo  |  T (C)   UR (%)   DHT     |  nivel ADC   mV    %tanque"));
  Serial.println(F("-------+---------------------------+---------------------------"));
}

void loop() {
  const uint32_t agora = millis();

  // ---- ADC do nivel, a 10 Hz -----------------------------------------
  if ((int32_t)(agora - g_proxAdc) >= 0) {
    g_proxAdc = agora + 100;

    const uint16_t v   = (uint16_t)analogRead(PIN_NIVEL);
    g_amostras[g_topo] = v;
    g_topo             = (g_topo + 1) % JANELA;
    if (g_n < JANELA) g_n++;
  }

  // ---- DHT22, respeitando o minimo de 2 s do sensor ------------------
  if ((int32_t)(agora - g_proxDht) >= 0) {
    g_proxDht = agora + INTERVALO_DHT_MS;

    const float t = g_dht.readTemperature();
    const float u = g_dht.readHumidity();
    g_lidasDht++;

    if (isnan(t) || isnan(u)) {
      g_falhasDht++;
      g_falhasSeguidas++;
    } else {
      g_falhasSeguidas = 0;
      g_tempC          = t;
      g_umid           = u;
    }
  }

  // ---- Uma linha por segundo -----------------------------------------
  if ((int32_t)(agora - g_proxLinha) >= 0) {
    g_proxLinha = agora + 1000;

    const Resumo r        = resume();
    const uint32_t mv     = analogReadMilliVolts(PIN_NIVEL);
    const bool dhtRespond = !isnan(g_tempC);

    Serial.printf("%5lus |  ", (unsigned long)(agora / 1000UL));
    if (dhtRespond) {
      Serial.printf("%6.1f  %6.1f   %s", g_tempC, g_umid, g_falhasSeguidas ? "falhou" : "ok    ");
    } else {
      Serial.print(F("    --      --   sem resposta"));
    }
    Serial.printf("  |  %8u  %4lu   %3u%%\n", r.media, (unsigned long)mv, tanquePct(r.media));

    // Bloco de diagnostico a cada dez linhas. A cada segundo seria ruido;
    // so no fim seria tarde para quem esta mexendo no fio agora.
    static uint8_t linhas = 0;
    if (++linhas >= 10) {
      linhas         = 0;
      const Resumo d = resume();
      Serial.println();
      Serial.printf("  nivel : min %u  max %u  media %u  espalhamento %u  -> %s\n", d.minimo,
                    d.maximo, d.media, d.espalhamento, diagnosticoNivel(d));
      Serial.printf("  DHT22 : %lu leituras, %lu falhas (%lu seguidas)  -> %s\n",
                    (unsigned long)g_lidasDht, (unsigned long)g_falhasDht,
                    (unsigned long)g_falhasSeguidas, diagnosticoDht());
      Serial.printf("  limiares atuais do tanque: vazio <=%d  cheio >=%d  (config.h)\n",
                    NIVEL_VAZIO_ADC, NIVEL_CHEIO_ADC);
      Serial.println();
    }
  }
}
