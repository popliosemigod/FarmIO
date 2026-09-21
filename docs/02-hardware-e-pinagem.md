# Hardware e pinagem

Placa de controle: **ESP32-C3** (4 MB de flash embarcada, USB-Serial/JTAG nativo).
Placa de visão: **ESP32-CAM AI-Thinker**.

> A v0.1 deste firmware foi escrita para a **ESP32 DevKit V1**, que continua
> compilando no ambiente `esp32dev`. A bancada tem um C3, e é ele que manda na
> pinagem desde a v0.2. A tabela da DevKit está preservada [no fim desta
> página](#a-pinagem-da-devkit-v1-preservada).

## As três restrições que mandam na tabela do C3

### 1. ADC — leitura analógica só em GPIO0..GPIO4

O C3 tem ADC1 em GPIO0–GPIO4 e ADC2 em GPIO5. O **ADC2 do C3 é pior que o do
ESP32 clássico**: além do conflito com o rádio, ele não tem suporte no driver do
IDF. Regra prática: leitura analógica só no ADC1.

Sobra o GPIO5 para uso digital — e é exatamente onde o DHT22 foi parar. Pino de
ADC ruim vira pino digital bom.

### 2. Strapping — GPIO2, GPIO8 e GPIO9 são lidos no boot

GPIO2 e GPIO8 precisam estar **altos**; GPIO9 baixo joga a placa no bootloader.
Nenhum deles aceita pull-down externo.

É por isso que o pino de acionamento da bomba — que **exige** pull-down, para a
bomba ficar desligada enquanto o C3 boota — não pode ser strapping. Ele é o
GPIO3.

E é por isso que o **SDA fica no GPIO8**: o barramento I2C já tem pull-up, que é
exatamente o nível que o strapping precisa no boot. O pino mais delicado da placa
vira o mais seguro.

### 3. São treze pinos, e só

GPIO11–GPIO17 estão na flash interna e não saem no conector. GPIO18/19 são o USB
nativo. Restam **0–10, 20 e 21**.

Contar os pinos *antes* de escolher os periféricos é o que evitou descobrir na
solda que faltavam dois.

## Tabela de ligação — ESP32-C3

| Função | GPIO | Tipo | Observação |
| --- | --- | --- | --- |
| Umidade do solo (AO) | **0** | ADC1_CH0 | alimentar o sensor em **3V3** |
| Nível do tanque (AO) | **1** | ADC1_CH1 | alimentar o sensor em **3V3** |
| Reserva analógica | 4 | ADC1_CH4 | livre — luminosidade, pH, segundo vaso |
| Bomba — IN1 do driver | **3** | saída LEDC | 1 kHz, **pull-down de 10 kΩ** |
| DHT22 dados | 5 | digital | pull-up de 10 kΩ para 3V3 |
| Anel WS2812 (dados) | 6 | saída | 16 pixels |
| Reset da ESP32-CAM | 7 | dreno aberto | pull-up de 10 kΩ |
| OLED SDA | **8** | I2C | strapping: o pull-up do I2C o mantém alto no boot |
| Botão de configuração | 9 | entrada | BOOT, já tem pull-up |
| OLED SCL | 10 | I2C | endereço 0x3C (o firmware tenta 0x3D também) |
| Enlace — RX | 20 | UART1 | ← TX da câmera (GPIO14 dela) |
| Enlace — TX | 21 | UART1 | → RX da câmera (GPIO15 dela) |

Pino deliberadamente livre: **GPIO2** (strapping alto, sem uso que justifique o
risco). O console e a gravação vão pelo **USB nativo** (GPIO18/19), o que é o que
libera a UART0 para o enlace — ver [04-enlace-c3-cam.md](04-enlace-c3-cam.md).

O C3 mini **não tem LED de placa em posição padronizada** entre fabricantes (GPIO7
na LOLIN, GPIO8 na SuperMini). Por isso `PIN_LED_PLACA` é −1 e o anel é o
indicador. Se a sua placa acender algo sozinha ao ligar o I2C, é o LED dela no
GPIO8 — é inofensivo, e custa 1–3 mA.

## A bomba usa um pino, não quatro

Na DevKit V1 a bomba ocupava quatro pinos: PWMA, AIN1, AIN2 e STBY do TB6612FNG.
No C3 não há quatro pinos para gastar com uma bomba que gira num sentido só.

O driver que está na bancada **não é o TB6612FNG**. É um módulo de ponte H dupla
pequeno, com header `IN1`..`IN4` e `GND`, **sem pinos de enable**. A ligação:

```
módulo     IN1  ←── GPIO3 do C3, com pull-down de 10 kΩ para GND
           IN2  ──── GND                  (sentido fixo, no cobre)
           IN3  ──── livre                (segundo canal sem fio)
           IN4  ──── livre
```

Com as duas entradas do canal em nível baixo, a ponte fica em roda livre e a bomba
não gira. É esse o estado que o pull-down garante durante todo o boot.

> **O pull-down deixou de ser reforço e virou o único mecanismo.** Com o TB6612FNG
> havia dois cadeados — o STBY em pull-down e o duty zero. Este módulo não tem
> enable nem STBY. Sem o resistor, o pino do C3 fica em alta impedância durante o
> boot inteiro e não há nada atrás para segurar a bomba.

O `config.h` cobre os dois arranjos: `BOMBA_PINO_UNICO` está definido só na
pinagem do C3.

### A bomba roda em 7 a 9 V, não em 12

O módulo aceita **até 11 V** na alimentação de potência, e a RS-385 é de **12 V
nominais**. Por um momento os dois números não conviveram. Resolvido por decisão
de bancada: **a bomba passa a ser alimentada em 7 a 9 V**, com folga sob o teto do
driver.

Motor CC aceita subtensão sem drama, e é isso que torna a decisão barata — girar
devagar não danifica nada. O que muda é a vazão:

| Tensão na bomba | Rotação e vazão, aprox. |
| --- | --- |
| 12 V (nominal) | 100% — o número do dimensionamento original |
| 9 V | ~75% |
| 7 V | ~58% |

**A consequência cai toda em `BOMBA_PASSO_MS`.** O pulso de 4 s foi dimensionado
para 12 V; com menos vazão, o mesmo pulso leva menos água. O número certo só sai
do ensaio com planta — medir quantos pulsos tiram o solo da faixa seca e ajustar.
Até lá, o tempo de pulso é mais um chute educado, na mesma condição dos limiares.

Some-se a isso a queda do próprio driver, que a bomba não vê: se o chip for
DRV8833 (MOSFET) são uns 0,4 V a 1 A; se for da família L9110 (bipolar), mais.
Alimentar o módulo com 9 V entrega algo entre 8 e 8,6 V na bomba.

#### O que ainda falta saber: a corrente

O teto de 11 V diz que o chip **não é um L298N**, apesar do nome com que o módulo
é vendido — o L298N aceita 46 V. Pelo teto, as candidatas prováveis são:

| Chip | Tensão | Corrente contínua por canal |
| --- | --- | --- |
| DRV8833 | 2,7–10,8 V | 1,5 A (2 A de pico) |
| L9110S / HG7881 | 2,5–12 V | 800 mA |

A corrente de partida de um motor CC é a de rotor travado, e ela **escala com a
tensão**: a RS-385 passa de 2 A em 12 V, o que dá cerca de 1,5 A em 9 V e 1,2 A em
7 V, por algumas dezenas de milissegundos. Rodar em 7–9 V já derrubou o problema
de "certamente demais" para "depende do chip" — um DRV8833 aguenta, um L9110S
fica no limite.

**Ler a marcação impressa no chip é o que fecha a conta**, e continua sendo passo
de bancada. Se for L9110S, a saída mais limpa é trocar a bomba por uma de
diafragma de 5 V (~350 mA), que é a mesma que o [orçamento de
energia](06-energia-usb.md) aponta como a única que irriga alimentada por USB.

## Alimentação

O projeto tem dois modos, e o firmware sabe em qual está (`ENERGIA_FONTE_USB`).

**Modo USB — o de hoje:**

```
Porta USB 5 V ──┬── ESP32-C3 (conector USB-C)
                ├── ESP32-CAM (5V)
                ├── anel WS2812 (VDD)
                └── OLED + DHT22 (via 3V3 do C3)
```

**Modo 12 V — o de campo:**

```
Fonte 12 V ──┬── driver da bomba ── bomba RS-385 12 V
             │
             └── LM2596 ─── 5 V ──┬── ESP32-C3
                                  ├── ESP32-CAM
                                  └── anel WS2812
```

O orçamento de corrente completo, com o que cabe e o que não cabe em cada modo,
está em [06-energia-usb.md](06-energia-usb.md). O resumo é curto: **a bomba de
12 V não roda em USB**, e o firmware bloqueia e diz por quê.

Três pontos que quebram a montagem se passarem batido:

1. **Sensores analógicos em 3V3, nunca em 5 V.** Alimentado em 5 V, o sensor de
   solo entrega até 5 V na saída analógica, e o ADC do ESP32-C3 aguenta 3,3 V. O
   pino morre — e morre calado, lendo valor fixo.
2. **Terra comum** entre C3, câmera, anel e fonte. Sem isso a leitura analógica
   flutua e a bomba liga sozinha.
3. **O anel de 16 WS2812 em brilho cheio puxa ~0,96 A.** É mais que a porta USB
   inteira. O firmware calcula o brilho a partir do orçamento e nunca passa disso.

## Leitura que não é leitura: sensor solto

Em 21/09/2026 apareceu um defeito que o bloqueio de energia vinha escondendo. Com
o sensor de solo solto, o pino encostou em 4095 — e o firmware classificou isso
como **"solo extremamente seco, irrigue"**. O tanque solto, também em 4095, virou
**"100% cheio"** — e tanque cheio libera a bomba. Os dois erros apontavam para o
mesmo lado: bomba ligada, sem água, por causa de um fio. Quem impedia era o
bloqueio de energia, e só por acaso; tirá-lo para o botão do app funcionar teria
exposto o defeito.

Desde então, leitura fora da faixa **fisicamente possível** não é leitura:

| Sensor | Faixa física em 3V3 | Encostou no teto | Encostou no chão |
| --- | --- | --- | --- |
| solo (capacitivo) | ~1,2 V a ~2,8 V → ADC ~1500 a ~3700 | **sem leitura** | **sem leitura** |
| nível (Funduino) | 0 a ~2,6 V → ADC 0 a ~3400 | **sem leitura** | tanque vazio — de verdade |

O corte é `ADC_PISO_VALIDO` (40) e `ADC_TETO_VALIDO` (4050), em `config.h`. No
nível, só o teto invalida: o chão é tanque vazio de verdade, e já bloqueia a bomba
pelo outro lado. Os dois defeitos caem no lado seguro.

**O limite desta regra.** Ela pega o pino que encosta num trilho, que foi o que a
bancada mostrou em 21/09. **Não pega** o pino que flutua no meio da faixa, que foi
o que a bancada mostrou em 09/09 — solo passeando entre 400 e 800. Para esse caso
não há regra de software confiável: um valor flutuante no meio da faixa é
indistinguível de uma leitura. A saída é de montagem — não ligar a bomba com
sensor desconectado — e, se um dia virar problema, um resistor de pull-down alto
na entrada analógica, que faz o sensor solto ler zero em vez de flutuar.

## Calibração — leia antes de confiar em qualquer leitura

Os limiares em [`include/config.h`](../include/config.h) são **ponto de partida,
não medida**:

```c
#define SOLO_SECO_ADC        2800
#define SOLO_BAIXO_ADC       2400
#define SOLO_ALTO_ADC        1600
#define SOLO_ENCHARCADO_ADC  1200
#define NIVEL_VAZIO_ADC       300
#define NIVEL_CHEIO_ADC      2600
```

Sensor capacitivo varia entre lotes, e o valor depende do substrato, da
profundidade de inserção e da tensão de alimentação. O procedimento:

1. gravar o ambiente `bancada` (`pio run -e bancada -t upload`);
2. ler o `solo_adc` pela serial com o sensor **no ar** → é o teto do seco;
3. repetir com o sensor em **terra encharcada** → é o piso do molhado;
4. dividir a faixa em cinco e atualizar os quatro limiares;
5. commitar com tipo `calib`, registrando o número medido no
   [diário](../diario.md).

Repetir para o tanque: vazio e cheio. Até isso ser feito, o firmware **funciona
mas não está correto** — ele irriga na hora errada.

> **Atenção ao migrar os números da DevKit V1.** O ADC do C3 tem 12 bits como o do
> ESP32 clássico, mas curva de atenuação e ruído diferentes. Os limiares
> calibrados numa placa **não** valem na outra.

## A pinagem da DevKit V1, preservada

Ambiente `esp32dev`. A regra ali é a clássica: **ADC2 (0, 2, 4, 12–15, 25–27)
devolve lixo com o Wi-Fi ligado**, então toda leitura analógica fica no ADC1
(32–39), e 34–39 são só entrada.

| Função | GPIO | Observação |
| --- | --- | --- |
| OLED SDA / SCL | 21 / 22 | |
| DHT22 | 4 | pull-up de 10 kΩ |
| Solo (AO) | 34 | ADC1_CH6, só entrada |
| Nível (AO) | 35 | ADC1_CH7, só entrada |
| Anel WS2812 | 27 | |
| Bomba PWMA / AIN1 / AIN2 / STBY | 26 / 25 / 33 / 14 | pinagem do TB6612FNG, que era o driver da v0.1 |
| Enlace RX / TX / RST | 16 / 17 / 19 | |
| Botão / LED / buzzer | 0 / 2 / 13 | |

Pinos livres de propósito: **6–11** (flash interna) e **12** (strapping MTDI —
nível alto no boot faz a placa tentar 1,8 V na flash e não subir).
