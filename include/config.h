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

#if !defined(FARMIO_PLACA_C3) && !defined(FARMIO_PLACA_ESP32DEV) && !defined(FARMIO_PLACA_CAM) && \
    !defined(FARMIO_PLACA_XIAO_CAM)
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
#define BOMBA_PWM_FREQ      1000
// TENSAO DA BOMBA, EM VOLTS.
//
// O driver aceita ate 11 V. A bomba RS-385 e de 12 V nominais, e por um
// momento os dois numeros nao conviveram - o registro esta no diario,
// entrada de 18/09/2026. Resolvido por decisao de bancada: a bomba passa
// a ser alimentada em 7 a 9 V, abaixo do teto do driver.
//
// MOTOR CC ACEITA SUBTENSAO SEM DRAMA, e isso e o que torna a decisao
// barata: girar devagar nao danifica nada. O que muda e a vazao, que cai
// junto com a rotacao - em 9 V a bomba entrega cerca de tres quartos do
// que entregaria em 12 V, e em 7 V pouco mais da metade.
//
// A CONSEQUENCIA CAI TODA EM BOMBA_PASSO_MS. O pulso de 4 s foi
// dimensionado para 12 V; com menos vazao, o mesmo pulso leva menos agua
// ao vaso, e o numero certo so sai do ensaio com a planta - medir quantos
// pulsos tiram o solo da faixa seca e ajustar. Ate la, este e mais um
// numero de chute educado, como os limiares.
#define BOMBA_DRIVER_VMAX_V 11  // teto do modulo - nao passar disso
#define BOMBA_TENSAO_V      9   // alvo de operacao (faixa util: 7 a 9)

#define BOMBA_PWM_BITS   10
#define BOMBA_PWM_MAX    1023
#define BOMBA_CANAL_LEDC 0

// Brilho maximo do anel. O limite real e calculado em tempo de execucao
// pelo orcamento de energia (energia.h) - este e so o teto de conforto
// visual, que veio da especificacao do SmartFarm.
#define ANEL_BRILHO 40  // 0..255

// =====================================================================
//  PINAGEM DA CAMERA - usada so pelo firmware da camera
//
//  DUAS PLACAS DE CAMERA POSSIVEIS, escolhidas por flag do platformio.ini,
//  pelo mesmo motivo das duas placas de controle: trocar de placa tem de
//  ser mexer em um arquivo so.
//
//    FARMIO_PLACA_XIAO_CAM  Seeed XIAO ESP32-S3 Sense - A PLACA DA BANCADA
//    FARMIO_PLACA_CAM       ESP32-CAM AI-Thinker - a da v0.2, mantida
//                           compilando pelo mesmo motivo da DevKit V1
// =====================================================================
#if defined(FARMIO_PLACA_CAM)
// ---------------------------------------------------------------------
//  ESP32-CAM AI-Thinker
//
//  Os pinos do sensor sao fixos pela placa e nao ha o que escolher. O
//  que foi escolhido e o par do enlace: GPIO14 e GPIO15, que na
//  AI-Thinker sao linhas do cartao SD - e nao ha cartao SD neste projeto.
//
//  POR QUE NAO GPIO1/GPIO3, que e o header de gravacao. Porque e por ali
//  que a ROM cospe o log de boot a 115200 toda vez que a camera reinicia,
//  e porque desligar o enlace para regravar a camera vira rotina.
//
//  GPIO15 e strapping (MTDO) e precisa estar alto no boot. Linha de RX
//  de UART em repouso E alta, entao o enlace mantem o nivel correto.
// ---------------------------------------------------------------------
#define CAM_PIN_ENLACE_TX  14  // -> RX do C3
#define CAM_PIN_ENLACE_RX  15  // <- TX do C3
#define CAM_PIN_LED_FLASH  4   // LED branco de 1 W: NUNCA ligar em USB
#define CAM_PIN_LED_ENLACE 33  // LED vermelho da placa, ativo em nivel baixo

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

// A OV2640 sai espelhada e de cabeca para baixo em relacao ao encaixe
// mecanico da AI-Thinker.
#define CAM_VFLIP   1
#define CAM_HMIRROR 1

#else
// ---------------------------------------------------------------------
//  Seeed XIAO ESP32-S3 Sense - A PLACA DA BANCADA desde 21/09/2026
//
//  ESP32-S3R8: 8 MB de PSRAM no proprio chip, 8 MB de flash, USB nativo.
//  O sensor fica na placa de expansao Sense e usa pinos internos - nenhum
//  dos onze da borda. Os pinos abaixo sao os da Seeed, os mesmos do
//  exemplo CameraWebServer do core Arduino-ESP32.
//
//  O ENLACE VAI EM D0/D1 (GPIO1/GPIO2), e a escolha foi por eliminacao:
//
//    D6/D7 (GPIO43/44) - sao a UART0, e a ROM cospe o log de boot no
//                        GPIO43 a cada reinicio, mesmo com o console no
//                        USB nativo. Mesmo motivo que tirou o enlace do
//                        header de gravacao da AI-Thinker.
//    D2    (GPIO3)     - strapping do S3.
//    D8-D10 (GPIO7-9)  - na placa Sense, sao o SPI do cartao SD.
//
//  Sobram D0 e D1, que nao tem funcao nenhuma na Sense.
//
//  SEM PINO DE RESET NA BORDA. O EN da XIAO so existe no botao de reset -
//  nao ha pino para o C3 pulsar. A escada de recuperacao do vaso continua
//  pulsando o GPIO7 dele, sem efeito ate alguem soldar um fio no botao; o
//  que segura a camera travada e o watchdog do proprio loop dela, em
//  main_cam.cpp.
// ---------------------------------------------------------------------
#define CAM_PIN_ENLACE_TX  1   // D0 -> RX do C3 (GPIO20)
#define CAM_PIN_ENLACE_RX  2   // D1 <- TX do C3 (GPIO21)
#define CAM_PIN_LED_FLASH  -1  // a XIAO nao tem flash
#define CAM_PIN_LED_ENLACE 21  // LED de usuario da XIAO, ativo em nivel baixo

#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  10
#define CAM_PIN_SIOD  40
#define CAM_PIN_SIOC  39
#define CAM_PIN_D7    48
#define CAM_PIN_D6    11
#define CAM_PIN_D5    12
#define CAM_PIN_D4    14
#define CAM_PIN_D3    16
#define CAM_PIN_D2    18
#define CAM_PIN_D1    17
#define CAM_PIN_D0    15
#define CAM_PIN_VSYNC 38
#define CAM_PIN_HREF  47
#define CAM_PIN_PCLK  13

// Orientacao NAO CONFIRMADA. Estes sao os valores do exemplo da Seeed
// para a OV2640; a primeira foto real diz se a imagem sai de cabeca para
// baixo. Se sair, e aqui que se corrige - e nao no classificador, que nao
// se importa com orientacao.
#define CAM_VFLIP     0
#define CAM_HMIRROR   0
#endif

// =====================================================================
//  ENLACE C3 <-> ESP32-CAM
// =====================================================================
// Quantos bytes na FIFO de hardware da UART disparam a interrupcao. O padrao
// do driver e 120 de 128: a FIFO enche em 11 ms a 115200 bps, entao sobram
// ~0,7 ms para atender - e o C3 tem um so nucleo, dividido com o Wi-Fi.
// Medido em 21/09/2026: 4 estouros de FIFO em 40 fotos, com o buffer de
// 4 kB da UART sem nenhum estouro. Com 32, sobram ~8 ms de folga.
#define ENLACE_FIFO_GATILHO 32

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

// A VISAO AINDA NAO FOI CALIBRADA COM FOTO REAL. Em 21/09/2026 a primeira
// foto de verdade - uma parede, um carretel de filamento, nenhuma planta -
// saiu classificada como "planta, 1000 permil", tres vezes seguidas. O
// modelo foi treinado em cena sintetica, e a primeira cena real o
// derrubou. Hipotese: o sensor entrega tom verde nas areas escuras, e o
// ExG normalizado, que divide pela soma dos canais, amplifica esse tom.
//
// Enquanto isto estiver em 0:
//   - a pagina e a serial mostram o veredito COM o aviso de nao calibrado;
//   - o alarme "nenhuma planta a vista" nao dispara - alarme de uma
//     medida que ja errou com confianca maxima e ruido, nao informacao.
//
// Passar para 1 so depois de retreinar com fotos reais - com e sem planta,
// tiradas pela propria camera ('F' no console dela) - e registrar no
// diario o acerto medido nelas.
#define VISAO_CALIBRADA 0

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

#define ENERGIA_C3_MA   95   // ESP32-C3 com o radio sempre acordado (sem modem sleep)
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

// A BOMBA TEM FONTE PROPRIA. Decisao de bancada de 18/09/2026: a RS-385
// passa a ser alimentada em 7 a 9 V por uma fonte separada, e nao pela
// USB que alimenta a logica. Com isso:
//
//   - a USB deixa de pagar a corrente da bomba, entao ela sai do
//     orcamento de energia e deixa de ser bloqueada por ele;
//   - a camera pode capturar com a bomba girando, porque os dois picos
//     ja nao disputam a mesma porta.
//
// O QUE A FONTE SEPARADA EXIGE NA MONTAGEM: GND comum entre a fonte da
// bomba, o driver e o ESP32-C3. O IN1 do driver e referenciado ao GND do
// C3; sem o terra comum o nivel logico flutua e a bomba liga sozinha.
//
// 0 volta ao modelo antigo, em que a bomba dividiria a porta USB.
#define BOMBA_FONTE_SEPARADA 1

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

// FAIXA FISICAMENTE POSSIVEL DO ADC. Fora dela, o numero nao e leitura -
// e sensor desconectado, fio solto ou alimentacao errada.
//
// Este bloco existe por causa de um defeito achado em 21/09/2026: com o
// sensor de solo solto, o pino encostou em 4095 e o firmware classificou
// isso como "solo extremamente seco - irrigue". O tanque solto, tambem em
// 4095, virou "100% cheio" - e tanque cheio libera a bomba. Os dois erros
// apontavam para o mesmo lado: bomba ligada, sem agua, por causa de um fio.
// Quem impedia era o bloqueio de energia, e so por acaso.
//
// Por que estes numeros sao seguros de usar como corte:
//   solo  - o capacitivo em 3V3 entrega de ~1,2 V (agua) a ~2,8 V (ar):
//           ADC ~1500 a ~3700. Ele nao encosta em nenhum dos trilhos.
//   nivel - o pente Funduino em 3V3 sai pelo emissor de um transistor,
//           entao o maximo fica ~0,7 V abaixo da alimentacao: ~2,6 V, ADC
//           ~3400. Encostar no teto e defeito. Encostar no chao, nao: e
//           tanque vazio de verdade, e ja bloqueia a bomba pelo outro lado.
//
// O LIMITE DESTA REGRA: ela pega o pino que encosta num trilho, que foi o
// que a bancada mostrou hoje. Nao pega o pino que flutua no meio da faixa,
// como a bancada mostrou em 09/09 (solo passeando entre 400 e 800). Para
// esse caso nao ha regra de software confiavel - ver docs/02.
#define ADC_PISO_VALIDO 40
#define ADC_TETO_VALIDO 4050

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

// ---- Bomba acionada pelo app ----------------------------------------
//  O botao do app NAO e uma chave que fica ligada. E um pedido com prazo,
//  renovado pela propria pagina enquanto ela estiver aberta:
//
//    LEASE - a pagina renova a cada 2 s. Se a renovacao parar - aba
//            fechada, celular bloqueado, roteador do celular caiu - a
//            bomba desliga sozinha em 6 s.
//    MAX   - teto absoluto, renovando ou nao. Em 9 V a RS-385 empurra
//            algo como 1 litro por minuto: 30 s sao meio litro, o que
//            ja e muita agua para um vaso.
//
//  POR QUE ASSIM, e nao liga/desliga simples: em campo aberto o unico
//  acesso e o roteador do celular, e ele cai. Uma chave que ficasse
//  ligada esperando o "desligar" que nunca chega esvaziaria o tanque.
#define BOMBA_MANUAL_MAX_MS   30000
#define BOMBA_MANUAL_LEASE_MS 6000

// ---- Foto sob demanda -----------------------------------------------
//  A foto vem pelo FIO, nao pelo Wi-Fi - ver docs/04.
//
//  O teto de tamanho subiu de 60 para 80 kB em 21/09/2026, depois da
//  primeira foto real: 49,9 kB de uma parede, com a qualidade antiga. Uma
//  planta tem mais detalhe que uma parede e comprime pior - o limite
//  antigo recusaria justamente a foto que o vaso existe para tirar. 80 kB
//  a 115200 bps sao ~7,5 s de fio, dentro do prazo de 15 s.
//
//  O C3 aloca o buffer inteiro de uma vez; com ~190 kB livres depois do
//  boot, 80 kB cabem num bloco so.
#define FOTO_TIMEOUT_MS 15000
#define FOTO_MAX_BYTES  81920  // 80 kB; recusa acima disso

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
  bool nivelValido;   // falso quando o ADC do nivel encostou no teto
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
};

struct EstadoBomba {
  bool ligada;
  uint16_t duty;  // 0..BOMBA_PWM_MAX
  uint32_t ligadaDesde;
  uint32_t ultimoPulso;
  uint32_t tempoTotalMs;  // quanto ja irrigou desde o boot
  uint16_t pulsos;
  const char* bloqueioAtual;  // por que nao esta irrigando, em texto

  // Acionamento pelo app. Contadores SEPARADOS dos do automatico, de
  // proposito: o pedido era que o botao nao influenciasse a logica do
  // vaso, e o jeito de garantir isso e o automatico nunca ver estes
  // numeros. 'ligada' continua valendo para os dois, porque ela descreve
  // o estado FISICO - e e ele que energia, tela e anel precisam saber.
  bool manual;
  uint32_t manualDesde;
  uint32_t manualAte;         // teto absoluto deste acionamento
  uint32_t manualRenovadoEm;  // ultima renovacao vinda do app
  uint32_t manualTotalMs;
  uint16_t manualAcionamentos;
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
