# Hardware e pinagem

Placa alvo: **ESP32 DevKit V1** (ESP32-D0WD-V3, 30 pinos, 4 MB de flash).

A pinagem abaixo não é arbitrária — ela sai de duas restrições do ESP32 que, se
ignoradas, produzem defeitos que parecem sensor com problema.

## A restrição que manda em tudo: ADC2 × Wi-Fi

O ESP32 tem dois blocos de conversor analógico. O **ADC2 é usado internamente
pelo rádio**: com o Wi-Fi ligado, `analogRead()` num pino de ADC2 devolve lixo ou
trava a chamada. São ADC2 os pinos **0, 2, 4, 12–15, 25, 26 e 27**.

O FarmIO vive com Wi-Fi ligado o tempo todo — o webserver é a interface do
cliente. Logo, **toda leitura analógica fica no ADC1 (GPIO 32–39)**. Os pinos de
ADC2 aparecem na tabela apenas como saída digital, onde não há conflito nenhum.

Segunda restrição: **GPIO 34–39 são só entrada** e não têm pull-up interno. Isso
serve perfeitamente para sensor analógico e não serve para botão.

## Tabela de ligação

| Função | GPIO | Tipo | Observação |
| --- | --- | --- | --- |
| OLED SDA | 21 | I2C | endereço 0x3C (o firmware tenta 0x3D também) |
| OLED SCL | 22 | I2C | |
| DHT22 dados | 4 | digital | pull-up de 10 kΩ para 3V3 |
| Umidade do solo (AO) | **34** | ADC1_CH6 | só entrada — alimentar o sensor em **3V3** |
| Nível do tanque (AO) | **35** | ADC1_CH7 | só entrada — alimentar o sensor em **3V3** |
| Anel WS2812 (dados) | 27 | saída | 16 pixels |
| Bomba — PWMA | 26 | saída LEDC | 20 kHz |
| Bomba — AIN1 | 25 | saída | |
| Bomba — AIN2 | 33 | saída | |
| Bomba — STBY | 14 | saída | **pull-down de 10 kΩ**: bomba desligada no boot |
| Botão de configuração | 0 | entrada | BOOT, já tem pull-up |
| LED da placa | 2 | saída | aceso enquanto irriga |
| Buzzer (opcional) | 13 | saída | o "alerta breve" da situação de risco |

Pinos deliberadamente livres: **6–11** (ligados à flash interna, usá-los trava a
placa) e **12** (strapping MTDI — nível alto no boot faz a placa tentar 1,8 V na
flash e não subir).

## Alimentação

```
Fonte 12 V ──┬── TB6612FNG (VM) ── bomba RS-385 12 V
             │
             └── LM2596 ─── 5 V ──┬── ESP32 (pino VIN)
                                  ├── anel WS2812 (VDD)
                                  └── TB6612FNG (VCC lógico via 3V3 do ESP32)

GND comum obrigatório entre fonte, driver, ESP32 e anel.
```

Três pontos que quebram a montagem se passarem batido:

1. **Sensores analógicos em 3V3, nunca em 5 V.** Alimentado em 5 V, o sensor de
   solo entrega até 5 V na saída analógica, e o ADC do ESP32 aguenta 3,3 V. O
   pino morre — e morre calado, lendo valor fixo.
2. **Terra comum.** Fonte de 12 V, driver, ESP32 e anel precisam compartilhar
   GND. Sem isso a leitura analógica flutua e a bomba liga sozinha.
3. **O anel de 16 WS2812 em brilho cheio puxa ~0,96 A.** A fonte de bancada de
   15 V/2 A do laboratório não entrega isso junto com a bomba. Por isso
   `ANEL_BRILHO` está em 40 de 255 — o que também atende ao pedido de não gerar
   desconforto visual.

## Por que uma ponte H para uma bomba de sentido único

O `TB6612FNG` está no estoque, tem saída MOSFET (queda baixa, aquece pouco) e
traz o pino `STBY`, que com pull-down garante bomba desligada enquanto o ESP32
boota. Um MOSFET avulso resolveria com menos peça e é a simplificação prevista
para quando houver PCB própria — mas trocar driver testado por driver não testado
antes do primeiro ensaio seria mexer em duas variáveis ao mesmo tempo.

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

## Duas placas, uma página

A ESP32-CAM não entra no mesmo firmware. Ela roda o exemplo `CameraWebServer` do
próprio core e serve o fluxo em `http://<ip-da-cam>:81/stream`. Esta ESP32
hospeda a página e embute esse fluxo num `<img>`.

Para apontar a página para a câmera, sem recompilar:

```
http://<ip-do-farmio>/cam?ip=192.168.0.55
```

A separação é da especificação original e a razão é boa: a ESP32-CAM sozinha não
aguenta servir página, sensores e streaming ao mesmo tempo.
