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
| Bomba — PWM | **3** | saída LEDC | 20 kHz, **pull-down de 10 kΩ** |
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

A solução mantém o driver testado e joga a direção para o cobre:

```
TB6612FNG      AIN1 ── 3V3        (sentido fixo, na placa)
               AIN2 ── GND
               STBY ── 3V3
               PWMA ── GPIO3 do C3, com pull-down de 10 kΩ para GND
```

Com PWMA em nível baixo, a saída do driver fica desligada independentemente de
STBY. O resistor de pull-down garante duty zero durante todo o boot — **a mesma
garantia que o STBY dava, com três pinos a menos**.

O `config.h` cobre os dois arranjos: `BOMBA_PINO_UNICO` está definido só na
pinagem do C3.

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
Fonte 12 V ──┬── TB6612FNG (VM) ── bomba RS-385 12 V
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
| Bomba PWMA / AIN1 / AIN2 / STBY | 26 / 25 / 33 / 14 | STBY com pull-down de 10 kΩ |
| Enlace RX / TX / RST | 16 / 17 / 19 | |
| Botão / LED / buzzer | 0 / 2 / 13 | |

Pinos livres de propósito: **6–11** (flash interna) e **12** (strapping MTDI —
nível alto no boot faz a placa tentar 1,8 V na flash e não subir).
