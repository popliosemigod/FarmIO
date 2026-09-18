// =====================================================================
//  FarmIO - config.h
//  Pinagem, limiares e parametros ajustaveis. Nenhuma logica mora aqui.
//
//  DUAS PLACAS DE CONTROLE POSSIVEIS. O projeto nasceu na ESP32 DevKit
//  V1 e migrou para o ESP32-C3, que e a placa que esta na bancada. As
//  duas pinagens moram neste arquivo, escolhidas por flag do
//  platformio.ini - trocar de placa continua sendo mexer em um arquivo
//  so, que e a regra do projeto.
// =====================================================================
#pragma once
#include <Arduino.h>

#if !defined(FARMIO_PLACA_C3) && !defined(FARMIO_PLACA_ESP32DEV) && !defined(FARMIO_PLACA_CAM)
#define FARMIO_PLACA_C3 1
#endif

// =====================================================================
//  PINAGEM
// =====================================================================
#if defined(FARMIO_PLACA_C3)
// ---------------------------------------------------------------------
//  ESP32-C3 (4 MB, USB-Serial/JTAG nativo) - A PLACA DA BANCADA
//
//  TRES RESTRICOES MANDAM NESTA TABELA:
//
//  1. ADC. O C3 tem ADC1 em GPIO0..GPIO4 e ADC2 em GPIO5. O ADC2 do C3 e
//     pior que o do ESP32 classico: alem do conflito com o radio, ele
//     nao tem suporte no driver do IDF. Regra pratica: leitura analogica
//     SO em GPIO0..GPIO4. Sobra GPIO5 para uso digital, e e exatamente
//     onde o DHT22 foi parar - pino de ADC ruim vira pino digital bom.
//
//  2. STRAPPING. GPIO2, GPIO8 e GPIO9 sao lidos no boot. GPIO2 e GPIO8
//     precisam estar ALTOS; GPIO9 baixo joga a placa no bootloader.
//     Nenhum deles pode receber pull-down externo. Por isso o pino de
//     acionamento da bomba, que EXIGE pull-down (bomba desligada
//     enquanto o C3 boota), nao pode ser strapping - ele e o GPIO3.
//
//  3. SO 13 PINOS. GPIO11..GPIO17 estao na flash interna e nao saem no
//     conector; GPIO18/19 sao o USB nativo. Restam 0-10, 20 e 21. Contar
//     os pinos ANTES de escolher os perifericos e o que evitou descobrir
//     na solda que faltavam dois.
//
//  A restricao 3 e a razao de a bomba usar UM pino em vez de quatro; a
//  explicacao esta em docs/02-hardware-e-pinagem.md.
// ---------------------------------------------------------------------

// ---- Barramento I2C (display OLED SSD1306 128x64) -------------------
//  SDA no GPIO8 e de proposito: GPIO8 e strapping e precisa estar alto
//  no boot, e o resistor de pull-up que todo barramento I2C ja tem faz
//  exatamente isso. O pino mais delicado da placa vira o mais seguro.
#define PIN_SDA      8
#define PIN_SCL      10
#define OLED_ADDR    0x3C  // 0x3D em alguns modulos; o firmware tenta os dois
#define OLED_LARGURA 128
#define OLED_ALTURA  64

// ---- Sensores -------------------------------------------------------
#define PIN_DHT     5  // ADC2 e ruim para analogico e otimo para digital
#define PIN_SOLO    0  // ADC1_CH0 - umidade do solo (saida analogica)
#define PIN_NIVEL   1  // ADC1_CH1 - nivel do tanque (saida analogica)
#define PIN_RESERVA 4  // ADC1_CH4 livre - luminosidade, pH, segundo vaso

// ---- Anel de LED RGB (16 pixels WS2812 / 5050) ----------------------
#define PIN_ANEL    6
#define ANEL_PIXELS 16

// ---- Bomba: UM pino ------------------------------------------------
//  O driver da bancada e um modulo de ponte H dupla pequeno, com header
//  IN1..IN4 e GND, SEM pinos de enable. A bomba gira num sentido so,
//  entao direcao nao precisa de fio: IN2 fica no GND e o ESP32-C3
//  controla apenas o IN1, por PWM.
//
//      IN1  <- GPIO3, com pull-down de 10 k para GND
//      IN2  -- GND
//      IN3, IN4 -- livres (o segundo canal nao tem fio)
//
//  O PULL-DOWN E O UNICO CADEADO, e por isso ele nao e opcional. O
//  projeto vinha de um TB6612FNG, que tinha STBY: eram dois mecanismos
//  garantindo bomba parada durante o boot - o STBY em pull-down e o duty
//  zero. Este modulo nao tem enable nem STBY, entao sobrou um. Sem o
//  resistor, o pino do C3 fica em alta impedancia durante todo o boot e
//  nao ha nada atras para segurar a bomba.
#define PIN_BOMBA_PWM    3  // vai no IN1 do modulo
#define BOMBA_PINO_UNICO 1

// TETO DE TENSAO DO DRIVER, EM VOLTS.
//
// Medido no proprio modulo por Henrique: ele aceita ate 11 V. Esse numero
// esta em conflito aberto com a bomba RS-385, que e de 12 V nominais - o
// registro completo do conflito e das saidas esta no diario, entrada de
// 18/09/2026. Enquanto ele nao for resolvido, NAO ligar a bomba de 12 V
// neste driver: 12 V na saida de um chip especificado para 11 V e como o
// projeto perde a ponte.
#define BOMBA_DRIVER_VMAX_V 11

// ---- Enlace com a ESP32-CAM -----------------------------------------
//  UART1 pela matriz de GPIO. O console vai pelo USB nativo (GPIO18/19),
//  entao estes dois pinos ficam so para a camera e o log do vaso nunca
//  entra como quadro do outro lado.
#define PIN_CAM_RX  20  // <- TX da camera (GPIO14 dela)
#define PIN_CAM_TX  21  // -> RX da camera (GPIO15 dela)
#define PIN_CAM_RST 7   // dreno aberto com pull-up de 10 k: reset da CAM

// ---- Interface local ------------------------------------------------
#define PIN_BOTAO     9  // BOOT: ja tem pull-up, vai ao GND quando pressionado
// O C3 mini nao tem LED de placa em posicao padronizada entre fabricantes
// (GPIO7 na LOLIN, GPIO8 na SuperMini). O anel de LED e o indicador.
#define PIN_LED_PLACA -1
#define PIN_BUZZER    -1  // GPIO2 sobrou, mas e strapping: ver o doc

#elif defined(FARMIO_PLACA_ESP32DEV)
// ---------------------------------------------------------------------
//  ESP32 DevKit V1 - a placa da v0.1, mantida compilando.
//  Aqui a regra e a classica: ADC2 (0, 2, 4, 12-15, 25-27) devolve lixo
//  com o Wi-Fi ligado, entao toda leitura analogica fica no ADC1 (32-39),
//  e 34-39 sao so entrada.
// ---------------------------------------------------------------------
#define PIN_SDA      21
#define PIN_SCL      22
#define OLED_ADDR    0x3C
#define OLED_LARGURA 128
#define OLED_ALTURA  64

#define PIN_DHT     4
#define PIN_SOLO    34  // ADC1_CH6
#define PIN_NIVEL   35  // ADC1_CH7
#define PIN_RESERVA 32  // ADC1_CH4

#define PIN_ANEL    27
#define ANEL_PIXELS 16

#define PIN_BOMBA_PWM  26
#define PIN_BOMBA_IN1  25
#define PIN_BOMBA_IN2  33
#define PIN_BOMBA_STBY 14  // pull-down de 10k: desligada no boot

#define PIN_CAM_RX  16
#define PIN_CAM_TX  17
#define PIN_CAM_RST 19

#define PIN_BOTAO     0
#define PIN_LED_PLACA 2
#define PIN_BUZZER    13
#endif

// FREQUENCIA DE PWM: 1 kHz, e nao os 20 kHz que estavam aqui.
//
// Os 20 kHz vinham do TB6612FNG, que e MOSFET e chaveia rapido - a
// escolha era ficar acima do audivel para a bomba nao "cantar". O driver
// real do projeto e um L298N mini, que e Darlington bipolar: a saida
// leva microssegundos para comutar, e a 20 kHz a ponte passa boa parte
// do tempo na regiao linear, onde ela nao chaveia - ela aquece.
//
// Na pratica o ruido nao volta: a bomba so e acionada em duty 100%,
// onde nao ha chaveamento nenhum. A frequencia so passa a importar no
// dia em que existir controle de vazao - e ai 1 kHz e o teto deste chip,
// nao uma preferencia.
#define BOMBA_PWM_FREQ   1000
#define BOMBA_PWM_BITS   10
#define BOMBA_PWM_MAX    1023
#define BOMBA_CANAL_LEDC 0

// Brilho maximo do anel. O limite real e calculado em tempo de execucao
// pelo orcamento de energia (energia.h) - este e so o teto de conforto
// visual, que veio da especificacao do SmartFarm.
#define ANEL_BRILHO 40  // 0..255

// =====================================================================
//  PINAGEM DA ESP32-CAM (AI-Thinker) - usada so pelo firmware da camera
//
//  Os pinos do sensor sao fixos pela placa e nao ha o que escolher. O
//  que foi escolhido e o par do enlace: GPIO14 e GPIO15, que na
//  AI-Thinker sao linhas do cartao SD - e nao ha cartao SD neste projeto.
//
//  POR QUE NAO GPIO1/GPIO3, que e o header de gravacao. Porque e por ali
//  que a ROM cospe o log de boot a 115200 toda vez que a camera reinicia,
//  e porque desligar o enlace para regravar a camera vira rotina. GPIO14
//  e GPIO15 deixam o header de gravacao livre: regravar a camera nao
//  exige desmontar nada.
//
//  GPIO15 e strapping (MTDO) e precisa estar alto no boot. Linha de RX
//  de UART em repouso E alta, entao o enlace mantem o nivel correto
//  sozinho - e se o C3 estiver desligado, o pull-up interno da conta.
// =====================================================================
#define CAM_PIN_ENLACE_TX    14  // -> RX do C3
#define CAM_PIN_ENLACE_RX    15  // <- TX do C3
#define CAM_PIN_LED_FLASH    4   // LED branco de 1 W: NUNCA ligar em USB
#define CAM_PIN_LED_VERMELHO 33

#define CAM_PIN_PWDN  32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  0
#define CAM_PIN_SIOD  26
#define CAM_PIN_SIOC  27
#define CAM_PIN_D7    35
#define CAM_PIN_D6    34
#define CAM_PIN_D5    39
#define CAM_PIN_D4    36
#define CAM_PIN_D3    21
#define CAM_PIN_D2    19
#define CAM_PIN_D1    18
#define CAM_PIN_D0    5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22

// =====================================================================
//  ENLACE C3 <-> ESP32-CAM
// =====================================================================
#define ENLACE_BAUD 115200  // 8N1. Doze bytes de veredito levam ~1,2 ms

// Cadencia da pergunta. Planta nao entra nem sai do vaso em dez
// segundos; perguntar mais rapido gastaria corrente da camera - que e o
// maior consumidor do conjunto - sem informacao nova nenhuma.
#define ENLACE_INTERVALO_MS 10000
#define ENLACE_TIMEOUT_MS   2500  // captura + classificacao cabem folgadas

// Tres perguntas sem resposta = camera muda. Uma so seria alarme falso a
// cada reboot dela; tres, a 10 s cada, dao 30 s de tolerancia.
#define ENLACE_FALHAS_PARA_MUDA 3

// Depois de tantas falhas seguidas, o vaso pulsa a linha de reset da
// camera. E a unica recuperacao possivel sem alguem na bancada.
#define ENLACE_FALHAS_PARA_RESET 6

// Pulso de reset em MICROssegundos, nao em milissegundos. O EN da ESP32
// precisa de alguns microssegundos em nivel baixo; 2 ms sao trezentas
// vezes o necessario e ainda cabem dentro de um loop que nao pode
// bloquear. Um pulso de 120 ms, que seria o instinto, seria 120 ms sem
// ler solo nem decidir sobre a bomba - preco alto para nada.
#define ENLACE_RESET_PULSO_US 2000
#define ENLACE_ESPERA_BOOT_MS 4000  // a CAM leva ~2,5 s ate responder

// =====================================================================
//  VISAO - limiares do classificador de planta
//  O modelo (os dez pesos) mora em lib/farmio_visao/pesos.cpp; aqui
//  ficam so os cortes, que sao ajuste de operacao e nao de modelo.
// =====================================================================
#define VISAO_PISO_EXG        40   // ExG minimo para um pixel ser "verde"
#define VISAO_BRILHO_MINIMO   25   // abaixo disso a cena e escura demais
#define VISAO_BRILHO_MAXIMO   245  // acima disso o quadro esta estourado
#define VISAO_BLOCO_VERDE_PCT 35
#define VISAO_LIMIAR_PLANTA   650  // permil
#define VISAO_LIMIAR_DUVIDA   350

// A esp32-camera entrega RGB565 com o byte alto primeiro. Se a imagem
// sair com azul e vermelho trocados no ensaio, este e o unico ajuste.
#define VISAO_BYTE_ALTO_PRIMEIRO 1

// A bomba EXIGE ver planta para irrigar?  Padrao: NAO.
//
//  A tentacao e obvia - vaso sem planta nao precisa de agua. O motivo de
//  estar desligado tambem: camera suja, camera as escuras ou camera
//  morta viram "nao ha planta", e o intertravamento deixaria a planta
//  secar por causa de uma lente empoeirada. Enquanto nao houver ensaio
//  medindo falso negativo com planta real, a visao AVISA e nao MANDA.
#define BOMBA_EXIGE_PLANTA 0

// =====================================================================
//  ENERGIA - o projeto inteiro em uma porta USB
//
//  Todo numero abaixo e de datasheet ou de medida publicada, e todo um
//  deles precisa ser confirmado com amperimetro na bancada. Ate la, o
//  orcamento e conservador de proposito: errar para menos apaga LED,
//  errar para mais reinicia a placa no meio da irrigacao.
//
//  O teto padrao e 500 mA porque e o que uma porta USB 2.0 promete sem
//  negociacao. Quem alimentar por carregador de celular pode subir para
//  1500 - e o vaso passa a poder acender o anel inteiro.
// =====================================================================
// De onde vem a energia. 1 = uma porta USB alimenta o conjunto inteiro,
// que e o modo em que o projeto esta hoje. 0 = fonte de 12 V com o
// LM2596, que e o modo em que a bomba de 12 V passa a existir.
// Trocar de fonte e trocar este numero e mais nada.
#define ENERGIA_FONTE_USB 1

#define ENERGIA_TETO_MA    500  // 500 = USB 2.0 | 900 = USB 3.0 | 1500+ = carregador
#define ENERGIA_MARGEM_PCT 20   // reserva para os picos de transmissao do radio

#define ENERGIA_C3_MA   80   // ESP32-C3 com Wi-Fi conectado, media
#define ENERGIA_CAM_MA  180  // ESP32-CAM capturando QVGA, media
#define ENERGIA_OLED_MA 20   // SSD1306 128x64 com metade dos pixels acesos
#define ENERGIA_DHT_MA  2

// Um WS2812 puxa ~60 mA com os tres canais em 255. Em brilho b e cor
// branca, ~60*b/255 por pixel.
#define ENERGIA_PIXEL_MA_CHEIO 60

// A bomba RS-385 e de 12 V e NAO roda em USB: nem por tensao, nem por
// corrente. Ver docs/06-energia-usb.md. Com uma bomba de diafragma de
// 5 V no lugar, mudar para 1 e ajustar a corrente medida.
#define BOMBA_EM_5V      0
#define ENERGIA_BOMBA_MA 350  // so vale quando BOMBA_EM_5V

// =====================================================================
//  LIMIARES DOS SENSORES
//
//  ATENCAO: os valores de solo e de nivel abaixo sao PONTO DE PARTIDA,
//  nao medida. Sensor capacitivo varia entre lotes, e o valor depende do
//  substrato, da profundidade de insercao e da tensao de alimentacao.
//  Calibrar na bancada e commitar com tipo `calib`, registrando o numero
//  medido no diario. Ate la, o firmware funciona mas nao esta correto.
// =====================================================================

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

// =====================================================================
//  TEMPOS  (ms) - nada aqui bloqueia; sao intervalos de agenda
// =====================================================================
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

// =====================================================================
//  ESTADOS
// =====================================================================
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
  RISCO_SENSOR_MUDO     = 1 << 4,
  RISCO_CAMERA_MUDA     = 1 << 5,
  RISCO_SEM_PLANTA      = 1 << 6
};

#define RISCO_QUANTOS 7

static const char* const RISCO_TITULO[] = {
    "TEMPERATURA ALTA",    "SOLO MUITO SECO",     "TANQUE VAZIO",          "SOLO ENCHARCADO",
    "SENSOR SEM RESPOSTA", "CAMERA SEM RESPOSTA", "NENHUMA PLANTA A VISTA"};

// =====================================================================
//  ESTADO GLOBAL COMPARTILHADO
//  Uma fonte unica da verdade: display, webserver e serial leem daqui,
//  entao nao existe o caso classico de a tela dizer uma coisa e a pagina
//  dizer outra.
// =====================================================================
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

// O que a camera diz. Separado de Leituras porque tem outra cadencia e
// outro modo de falha: sensor mudo e camera muda nao sao a mesma avaria.
struct Visto {
  bool enlaceOk;           // a camera respondeu ao ultimo ping
  bool temPlanta;          // veredito ja filtrado no tempo
  uint16_t probabilidade;  // permil, do ultimo quadro
  uint16_t mediaFiltrada;  // permil, media da janela
  uint16_t cobertura;      // permil de verde no quadro
  uint8_t classe;          // Visao::Classe
  uint8_t flags;           // luz baixa, estourado, falha
  uint16_t msCamera;       // quanto a camera levou para classificar
  uint32_t ultimoQuadroEm;
  uint16_t quadros;
  uint16_t falhas;  // perguntas sem resposta desde o boot
  uint16_t resets;  // quantas vezes o vaso reiniciou a camera
  char ip[16];      // IP da camera, anunciado por ela mesma
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
extern Visto V;
extern EstadoBomba B;
extern uint8_t riscosAtivos;

// Versao do firmware - sobrescrita pelo platformio.ini
#ifndef FARMIO_VERSAO
#define FARMIO_VERSAO "0.2.0-dev"
#endif

#ifndef FARMIO_NOME
#define FARMIO_NOME "farmio-01"
#endif
