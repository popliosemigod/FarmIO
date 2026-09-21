# O projeto inteiro numa porta USB

O pedido era esse. Ele tem uma parte que dá e uma parte que não dá, e o firmware
precisa saber qual é qual — porque a parte que não dá, se for tentada, não falha
com mensagem de erro: **reinicia a placa por subtensão no meio da irrigação**, e o
vaso volta achando que nunca irrigou.

Este documento é o orçamento de corrente do vaso tratado como restrição de
projeto. O código que o aplica é [`include/energia.h`](../include/energia.h).

> **Todo número desta página é de datasheet ou de cálculo, nenhum é de
> amperímetro.** Enquanto não houver medida na bancada, o orçamento erra de
> propósito para o lado conservador: errar para menos apaga LED, errar para mais
> reinicia a placa.

> **Atualização de 21/09/2026: a bomba saiu da porta USB.** Por decisão de
> bancada, a RS-385 passou a ter fonte própria de 7 a 9 V
> (`BOMBA_FONTE_SEPARADA`). A USB continua alimentando só a lógica — C3, câmera,
> tela, sensores, anel —, e o orçamento abaixo continua valendo para ela. O que
> muda: a bomba deixa de ser bloqueada por energia, deixa de descontar do brilho
> do anel, e a câmera pode capturar com a bomba girando.
>
> A fonte separada exige **terra comum** entre a fonte da bomba, o driver e o C3.
> O `IN1` do driver é referenciado ao GND do C3; sem o terra comum o nível lógico
> flutua e a bomba liga sozinha.
>
> A câmera também perdeu o rádio na mesma data — a foto passou a vir pelo fio.
> Os 180 mA dela na tabela eram "capturando QVGA com Wi-Fi"; sem Wi-Fi o número
> real deve ser menor. Fica como está até o amperímetro dizer quanto: errar para
> mais apaga LED, errar para menos reinicia a placa.

## O que cabe

Porta USB 2.0 entrega 500 mA sem negociação nenhuma. O consumo médio do conjunto:

| Item | Corrente |
| --- | --- |
| ESP32-C3 com Wi-Fi conectado | 80 mA |
| ESP32-CAM capturando QVGA | 180 mA |
| OLED SSD1306 128×64 | 20 mA |
| DHT22 | 2 mA |
| **soma** | **282 mA** |
| margem de 20% para picos de rádio | 100 mA |
| **sobra para o anel de LED** | **118 mA** |

Cento e dezoito miliamperes dão **brilho 31 de 255** no anel de 16 pixels — abaixo
dos 40 que a especificação do SmartFarm pedia.

O firmware **não ignora esse teto**: ele calcula o brilho a partir do orçamento, a
cada quadro, e sobe ou desce um degrau por vez para que a mudança seja uma rampa e
não um salto. Trocar a porta por um carregador de 1,5 A faz o anel voltar aos 40
sem recompilar nada — é só mudar `ENERGIA_TETO_MA`.

A câmera entra e sai da conta conforme o enlace vive ou morre. Isso foi observado
no ensaio de 09/09/2026: com a câmera desligada, o vaso mediu-se em **252 mA** e
liberou o anel para os 40 de conforto visual; com a câmera no ar, o mesmo cálculo
dá 31.

## O que não cabe: a bomba de 12 V

A RS-385 é de 12 V. Uma porta USB entrega 5 V — **não há o que negociar em
tensão**. E se houvesse um elevador de 5 V para 12 V, a conta de entrada seria:

```
12 V × 1,5 A ÷ 5 V ÷ 0,85 (rendimento) = 4,2 A na porta USB
```

Oito vezes o que a porta promete. Portanto, com `ENERGIA_FONTE_USB` em 1, a bomba
fica **bloqueada por projeto**, e o bloqueio aparece na tela e no JSON com o motivo
escrito:

```json
"bomba_bloqueio": "bomba de 12 V nao roda em USB"
```

Bloqueio silencioso é o pior tipo — para quem olha o vaso, parece defeito. Essa
regra já valia para os intertravamentos em
[03-logica-de-operacao.md](03-logica-de-operacao.md) e vale igual aqui.

## As três saídas, com a conta de cada uma

### 1. Carregador de 5 V / 2 A + bomba de diafragma de 5 V — *recomendada*

```
base com a câmera .......  282 mA
bomba de 5 V ............  350 mA
anel em brilho 31 .......  117 mA
margem de 20% ...........  400 mA
------------------------------------
total .................. 1 149 mA   cabe em 2 000 mA
```

É a única configuração em que o vaso **irriga** alimentado por USB. Exige trocar
`BOMBA_EM_5V` para 1, `ENERGIA_TETO_MA` para 2000, e a bomba física.

Durante o pulso o anel apaga sozinho: o gerente de energia desconta a bomba antes
de calcular o brilho.

### 2. Porta USB 2.0 de 500 mA — *o estado de hoje*

Tudo funciona **menos a irrigação**. Sensores, tela, página, vídeo e detecção de
planta cabem com folga. Mesmo uma bomba de 5 V não cabe aqui:

```
disponível ............. 500 − 100 (margem) = 400 mA
base com a câmera ......                      282 mA
sobra ..................                      118 mA   < 350 mA da bomba
```

É por isso que a recomendação acima fala em carregador e não em porta de
computador. Não é preciosismo: são 232 mA de diferença.

### 3. Fonte de 12 V + LM2596 — *o projeto original*

`ENERGIA_FONTE_USB` em 0. A bomba de 12 V volta a existir, o anel vai a 40 e o
orçamento deixa de ser restrição. É a configuração de campo; a USB é a
configuração de bancada e de demonstração.

## Picos coincidentes

Corrente média é só metade do problema. Dois picos que caem no mesmo instante
somam, e é a soma que derruba a porta. O firmware serializa os dois maiores:

- **enquanto a bomba estiver girando, não se pede quadro à câmera.** Dez segundos
  de atraso na detecção de planta não custam nada; dois picos somados custam um
  reinício por subtensão.
- **o anel nasce apagado** e só sobe ao brilho de regime depois do boot. Acender
  em brilho cheio seria pedir 150 mA no instante em que a câmera também está
  inicializando.
- **o LED de flash de 1 W da ESP32-CAM fica desligado explicitamente**, no
  `setup()` dela. Ele sozinho puxa mais que a placa inteira.

## Adaptação a porta mentirosa

Cabo fino, hub barato e porta de teclado entregam menos do que dizem, e isso não
aparece em datasheet nenhum. Se a placa reiniciar por subtensão, o vaso:

1. detecta `ESP_RST_BROWNOUT` no boot;
2. incrementa um contador gravado na NVS;
3. **desce o teto em 15%** para cada queda registrada, até o piso de 250 mA;
4. segue funcionando com menos LED, em vez de entrar em laço de reinício.

É a única adaptação que ele consegue fazer sozinho. Abaixo de 250 mA nem o C3 com
a câmera cabem, e o problema deixa de ser orçamento e passa a ser cabo ou fonte —
caso para a bancada, não para o firmware.

## Conferir na bancada

O JSON de `/sensores` publica a estimativa do próprio consumo:

```json
"energia_teto_ma": 500, "energia_ma": 252, "anel_brilho": 40
```

O procedimento é comparar `energia_ma` com um amperímetro em série no cabo USB, em
três estados: anel apagado, anel em regime, e (quando houver) bomba girando. **A
divergência entre previsto e medido é o que corrige a tabela** do
[`config.h`](../include/config.h) — e é ela que vai para o [diário](../diario.md),
com tipo de commit `calib`.
