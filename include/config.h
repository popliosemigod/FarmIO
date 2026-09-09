// =====================================================================
//  FarmIO - config.h
//  Pinagem, limiares e parametros ajustaveis. Nenhuma logica mora aqui.
//  Placa alvo: ESP32 DevKit V1 (ESP32-D0WD-V3, 30 pinos)
// =====================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------
//  A REGRA QUE MANDA NA PINAGEM: ADC2 x Wi-Fi
//
//  O ESP32 tem dois blocos de ADC. O ADC2 e usado internamente pelo
//  radio: com o Wi-Fi ligado, analogRead() em pino de ADC2 devolve lixo
//  ou trava. Sao ADC2 os pinos 0, 2, 4, 12-15, 25, 26 e 27.
//
//  O FarmIO vive com Wi-Fi ligado o tempo todo (webserver e a interface
//  do cliente). Logo, TODA leitura analogica fica no ADC1: GPIO 32-39.
//  Os pinos de ADC2 aparecem abaixo so como saida digital, onde nao ha
//  conflito nenhum.
//
//  Pinos 34-39 sao SO ENTRADA e nao tem pull-up interno - o que serve
//  perfeitamente para sensor analogico e nao serve para botao.
// ---------------------------------------------------------------------

// ---- Barramento I2C (display OLED SSD1306 128x64) -------------------
#define PIN_SDA      21
#define PIN_SCL      22
#define OLED_ADDR    0x3C  // 0x3D em alguns modulos; o firmware tenta os dois
#define OLED_LARGURA 128
#define OLED_ALTURA  64

// ---- Sensores -------------------------------------------------------
#define PIN_DHT   4   // DHT22 - digital, com pull-up de 10k para 3V3
#define PIN_SOLO  34  // ADC1_CH6 - umidade do solo (saida analogica)
#define PIN_NIVEL 35  // ADC1_CH7 - nivel do tanque (saida analogica)

// ---- Anel de LED RGB (16 pixels WS2812 / 5050) ----------------------
#define PIN_ANEL    27
#define ANEL_PIXELS 16
// Brilho baixo de proposito: o pedido e que a LED nao gere desconforto
// visual. 16 pixels em brilho cheio tambem puxam ~0,96 A, que a fonte de
// bancada de 2 A nao entrega junto com a bomba.
#define ANEL_BRILHO 40  // 0..255

// ---- Bomba d'agua (RS-385 12 V) via TB6612FNG, canal A --------------
//  Por que uma ponte H para uma bomba que gira num sentido so: o
//  TB6612FNG e o driver que temos em estoque, tem saida MOSFET (queda
//  baixa, aquece pouco) e ja traz o STBY, que garante bomba desligada
//  enquanto o ESP32 boota. Um MOSFET avulso resolveria com menos peca -
//  fica como simplificacao para a versao com PCB propria.
#define PIN_BOMBA_PWM  26
#define PIN_BOMBA_IN1  25
#define PIN_BOMBA_IN2  33
#define PIN_BOMBA_STBY 14  // pull-down de 10k: desligada no boot

#define BOMBA_PWM_FREQ   20000  // 20 kHz: acima do audivel, a bomba nao "canta"
#define BOMBA_PWM_BITS   10
#define BOMBA_PWM_MAX    1023
#define BOMBA_CANAL_LEDC 0

// ---- Interface local ------------------------------------------------
#define PIN_BOTAO     0  // BOOT: ja tem pull-up, vai ao GND quando pressionado
#define PIN_LED_PLACA 2
#define PIN_BUZZER    13  // opcional - o "pequeno alerta" da situacao de risco

// ---------------------------------------------------------------------
//  LIMIARES
//
//  ATENCAO: os valores de solo e de nivel abaixo sao PONTO DE PARTIDA,
//  nao medida. Sensor capacitivo varia entre lotes, e o valor depende do
//  substrato, da profundidade de insercao e da tensao de alimentacao.
//  Calibrar na bancada e commitar com tipo `calib`, registrando o numero
//  medido no diario. Ate la, o firmware funciona mas nao esta correto.
// ---------------------------------------------------------------------

// Solo: sensor capacitivo alimentado em 3V3. Leitura ALTA = seco.
// Alimentar em 5 V colocaria ate 5 V no ADC de 3,3 V e mataria o pino.
#define SOLO_SECO_ADC       2800  // acima disso: extremamente baixa (irriga)
#define SOLO_BAIXO_ADC      2400
#define SOLO_ALTO_ADC       1600
#define SOLO_ENCHARCADO_ADC 1200  // abaixo disso: extremamente alta (bloqueia)

// Nivel do tanque: sensor resistivo tipo pente (Funduino). Leitura ALTA
// = mais agua tocando as trilhas.
#define NIVEL_VAZIO_ADC 300   // <= isso conta como tanque vazio
#define NIVEL_CHEIO_ADC 2600  // >= isso conta como 100%

// Temperatura: acima disso entra em SITUACAO DE RISCO (spec do SmartFarm)
#define TEMP_ALTA_C 30.0f

// ---------------------------------------------------------------------
//  TEMPOS  (ms) - nada aqui bloqueia; sao intervalos de agenda
// ---------------------------------------------------------------------
#define INTERVALO_DHT_MS    2500  // DHT22 nao aceita mais de 1 leitura / 2 s
#define INTERVALO_SOLO_MS   1000
#define INTERVALO_NIVEL_MS  1000
#define INTERVALO_TELA_MS   200   // taxa de redesenho do OLED
#define INTERVALO_SERIAL_MS 3000  // heartbeat de telemetria pela serial

#define TELA_RISCO_MS           4000   // quanto tempo a tela de risco interrompe
#define TELA_RISCO_INTERVALO_MS 30000  // de quanto em quanto ela reaparece

#define BOMBA_PASSO_MS 4000  // pulso de irrigacao
#define BOMBA_DESCANSO_MS \
  20000                         // espera entre pulsos: a agua leva tempo
                                // para percolar ate o sensor. Sem isso a
                                // malha irriga demais e afoga a planta.
#define BOMBA_LIMITE_MS 120000  // teto absoluto de bomba ligada por ciclo

// ---------------------------------------------------------------------
//  ESTADOS
// ---------------------------------------------------------------------
enum FaixaSolo : uint8_t {
  SOLO_EXTREMAMENTE_BAIXA = 0,
  SOLO_BAIXA,
  SOLO_ESTAVEL,
  SOLO_ALTA,
  SOLO_EXTREMAMENTE_ALTA,
  SOLO_INVALIDO
};

static const char* const SOLO_NOME[] = {"EXTREM. BAIXA", "BAIXA",        "ESTAVEL",
                                        "ALTA",          "EXTREM. ALTA", "SEM LEITURA"};

// Riscos sao bits: podem valer ao mesmo tempo (tanque vazio com calor, por
// exemplo). Tratar como enum simples esconderia o segundo risco.
enum Risco : uint8_t {
  RISCO_NENHUM          = 0,
  RISCO_TEMPERATURA     = 1 << 0,
  RISCO_SOLO_SECO       = 1 << 1,
  RISCO_TANQUE_VAZIO    = 1 << 2,
  RISCO_SOLO_ENCHARCADO = 1 << 3,
  RISCO_SENSOR_MUDO     = 1 << 4
};

static const char* const RISCO_TITULO[] = {"TEMPERATURA ALTA", "SOLO MUITO SECO", "TANQUE VAZIO",
                                           "SOLO ENCHARCADO", "SENSOR SEM RESPOSTA"};

// ---------------------------------------------------------------------
//  ESTADO GLOBAL COMPARTILHADO
//  Uma fonte unica da verdade: display, webserver e serial leem daqui,
//  entao nao existe o caso classico de a tela dizer uma coisa e a pagina
//  dizer outra.
// ---------------------------------------------------------------------
struct Leituras {
  float temperaturaC;  // NAN quando o DHT22 nao respondeu
  float umidadeArPct;
  uint16_t soloAdc;
  uint8_t soloFaixa;  // FaixaSolo
  uint16_t nivelAdc;
  uint8_t tanquePct;  // 0..100
  bool dhtOk;
  uint16_t dhtFalhas;
  uint32_t atualizadoEm;
};

struct EstadoBomba {
  bool ligada;
  uint16_t duty;  // 0..BOMBA_PWM_MAX
  uint32_t ligadaDesde;
  uint32_t ultimoPulso;
  uint32_t tempoTotalMs;  // quanto ja irrigou desde o boot
  uint16_t pulsos;
  const char* bloqueioAtual;  // por que nao esta irrigando, em texto
};

extern Leituras L;
extern EstadoBomba B;
extern uint8_t riscosAtivos;

// Versao do firmware - sobrescrita pelo platformio.ini
#ifndef FARMIO_VERSAO
#define FARMIO_VERSAO "0.1.0-dev"
#endif

#ifndef FARMIO_NOME
#define FARMIO_NOME "farmio-01"
#endif
