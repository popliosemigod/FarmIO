# Diário de bordo — FarmIO

Registro de cada iteração do ciclo de tentativa e erro. O formato é fixo para que
as entradas sejam comparáveis entre si e ao longo do tempo.

O campo que mais importa é a **divergência** entre previsto e medido: é ela que
corrige o modelo teórico da próxima iteração. Entrada sem divergência anotada é
entrada pela metade.

---

## Modelo de entrada

### AAAA-MM-DD — Título curto da iteração

**Alvo:** o que esta iteração precisava alcançar, de forma mensurável.

**Previsão:** o número calculado antes do ensaio.

**O que foi feito:** montagem, ajuste ou código alterado.

**Medido:** o resultado real.

**Divergência:** previsto × medido, e a hipótese para a diferença.

**Decisão:** o que muda na próxima iteração.

**Evidência:** caminho dos arquivos em `evidencias/`.

---

## Entradas

### 2026-09-02 — Firmware v0.1 a partir da especificação do SmartFarm

**Alvo:** transformar a especificação do SmartFarm, que existia como documento
solto no Drive, em firmware que compila e em repositório com o padrão do
laboratório. Critério de aceitação: `pio run` limpo e nenhum segredo versionado.

**Previsão:** o binário ficaria em torno de 60% da flash de 1,31 MB da DevKit V1,
por causa do servidor HTTP, do Wi-Fi e das três bibliotecas gráficas (SSD1306,
GFX, NeoPixel).

**O que foi feito:** repositório criado com o padrão do laboratório — mesmos
`cz.toml`, `.pre-commit-config.yaml`, `.clang-format`, `.editorconfig`,
`.gitattributes` e CI do Jaspy, com o escopo do `clang-format` apontado para
`src/` e `include/`. Firmware escrito em seis módulos (`config`, `sensores`,
`bomba`, `anel`, `tela`, `web`) implementando as cinco faixas de solo, os
intertravamentos da bomba, as duas telas, a linguagem de cor do anel e a rota
`/sensores` com o vídeo da ESP32-CAM embutido.

**Medido:**

| Ambiente | RAM | Flash | Tempo de build |
| --- | --- | --- | --- |
| `esp32dev` | 14,1% (46,3 kB de 320 kB) | 64,4% (844 kB de 1,31 MB) | 43,7 s |

**Divergência:** a previsão de flash ficou 4,4 pontos abaixo do medido (60%
previstos × 64,4% reais). A diferença provável é a página HTML embutida em
PROGMEM, que não entrou na conta inicial — são ~3,5 kB de texto, mais o overhead
do `WebServer`. Não é problema: sobram 466 kB, espaço suficiente para o
diagnóstico de imagem que ainda vai entrar.

**Decisão:** o firmware **não** usa a biblioteca `jaspa-core` do laboratório nesta
versão. O motivo é concreto: ela mora no repositório Jaspy, que é privado, e o
caminho por `symlink://` depende do layout de pastas da máquina — o CI deste
repositório não conseguiria resolver. A integração acontece quando a biblioteca
ganhar repositório próprio. Até lá, este projeto segue o mesmo **padrão** sem
depender do mesmo **código**.

**Pendência que importa mais que o resto:** os limiares de solo e de tanque em
`include/config.h` são chute educado. Enquanto não forem calibrados com o sensor
real, o vaso irriga na hora errada — o firmware está correto, os números não.

**Evidência:** saída de `pio run -e esp32dev`; commits desta data.

---

### 2026-09-09 — Duas placas ligadas, um classificador treinado e o orçamento de USB

**Alvo:** três coisas, na ordem em que dependem uma da outra. (1) Ligar o ESP32-C3
e a ESP32-CAM de forma estável. (2) Fazer o vaso reconhecer que há uma planta na
frente. (3) Ter o projeto inteiro funcionando alimentado por USB. Critério de
aceitação: os cinco ambientes compilando, o firmware do vaso rodando numa placa
real e um número medido para cada uma das três coisas.

**Descoberta que mudou o escopo antes de começar:** a placa na bancada é um
**ESP32-C3** (rev v0.4, 4 MB, USB-Serial/JTAG, na COM7), não a DevKit V1 que a
v0.1 assume. O C3 tem 13 pinos utilizáveis contra 25 da DevKit, e isso não é
detalhe de migração — é restrição de projeto. Forçou a bomba a sair de quatro
pinos para um.

**Previsão, anotada antes dos ensaios:**

| | Previsto |
| --- | --- |
| Acerto do classificador na reserva | 85–90% |
| Tempo de extração por quadro no C3 | 25–40 ms |
| Consumo do conjunto em USB, sem bomba | ~280 mA |
| Brilho do anel que sobra em porta de 500 mA | ~30 de 255 |
| Flash do firmware do vaso | ~70% |

**O que foi feito:**

- **Enlace por UART, não por Wi-Fi.** Duas bibliotecas portáveis novas em `lib/`:
  `farmio_enlace` (quadro com preâmbulo, versão, tipo, CRC-16/CCITT e receptor que
  ressincroniza) e `farmio_visao`. C++11 puro, sem Arduino — a mesma implementação
  roda nos dois lados do fio, o que elimina a divergência de protocolo entre
  placas. Razões da escolha em `docs/04-enlace-c3-cam.md`.
- **Ambiente `autoteste`.** Um quinto ambiente que roda no C3 sem nada ligado
  nele: exercita o protocolo contra lixo e erro de bit, gera 312 cenas sintéticas,
  extrai as características e **treina a regressão logística na própria placa**.
  Não há compilador de host nesta máquina, e treinar onde se executa elimina o
  erro clássico de o modelo acertar no notebook e errar no microcontrolador.
- **Firmware da ESP32-CAM** (`src/main_cam.cpp`): captura em JPEG, serve o vídeo
  pelo servidor do IDF em tarefa própria e decodifica um quadro a cada dez
  segundos para classificar. Tarefa separada porque, no mesmo loop, um navegador
  aberto seguraria a resposta ao vaso e o vaso reiniciaria a câmera por causa de
  uma aba.
- **Gerente de energia** (`include/energia.h`): o brilho do anel passa a ser
  calculado a partir do orçamento de corrente, e a bomba de 12 V é bloqueada com
  motivo escrito quando a fonte é USB.

**Medido:**

| Ensaio | Previsto | Medido | |
| --- | --- | --- | --- |
| Quadro corrompido em 1 bit, varredura completa | 100% recusados | **152/152 (100%)** | ✅ |
| Acerto do classificador, conjunto reservado | 85–90% | **97,9%** (94/96) | ✅ |
| Falso negativo | — | **0** | |
| Extração por quadro, no C3 | 25–40 ms | **11,5 ms** | ✅ |
| Consumo estimado em USB, câmera fora do ar | ~280 mA | **252 mA** | |
| Brilho do anel, câmera no ar / fora do ar | ~30 | **31 / 40** | ✅ |
| Flash do vaso (`c3`) | ~70% | **65,0%** (852 kB) | ✅ |
| RAM do vaso | — | 16,8% (54,9 kB) | |
| Flash da câmera (`cam`) | — | 15,1% de 3 MB (474 kB) | |

**Divergência 1 — extração 2,6 vezes mais rápida que o previsto.** A previsão de
25–40 ms saiu de estimar operação por pixel sobre a QVGA inteira. A implementação
subamostra para uma grade de 80×60, que são 16 vezes menos amostras, e a previsão
simplesmente não levou isso em conta. Consequência prática: há folga para subir a
grade se o acerto de campo pedir.

**Divergência 2 — acerto acima do previsto, e é ela que merece desconfiança.**
97,9% num banco que eu mesmo gerei mede o gerador tanto quanto o modelo. O número
honesto desta linha é o segundo: **zero falso negativo em 96 amostras
reservadas**, com os positivos difíceis (muda pequena, folhagem clorótica,
folhagem na sombra) dentro do conjunto. Acerto de campo é outra medida e ainda não
existe.

**O ciclo de tentativa e erro, que é o registro que importa:**

| # | Modelo | Reserva | O que o resultado revelou |
| --- | --- | --- | --- |
| 1 | Pesos chutados, 9 características | 70,8% | ponto de partida |
| 2 | Treino livre | 95,8% | peso **negativo** em `exgMedio` |
| 3 | Restrição de sinal | 91,7% | peso −2,77 em `brilho` |
| 4 | + `calor`, + textura relativa | **97,9%** | zero falso negativo |

As duas iterações do meio são o achado da sessão. Na 2, o modelo chegou a 95,8%
tendo aprendido que **verde demais é suspeito** — verdade dentro do banco, onde os
negativos difíceis (pano e plástico) são os mais verdes de todos, e falso fora
dele: derrubaria a primeira planta viçosa no sol. Na 3, com os sinais impostos,
ele achou o segundo atalho: "cena escura, provavelmente planta". Os dois foram
fechados por restrição vinda da física, não por mais treino.

A iteração 4 tem a correção técnica que mais rendeu, e que eu deveria ter visto
antes: `bordas` e `exgDesvio` mediam textura em unidades **absolutas** de ExG.
Folha clorótica tem ExG de ~84 contra ~168 de folha sadia, então toda a textura
medida em cima dele encolhia junto, e o classificador lia "folha amarelada tem
metade da textura de uma folha". Ela tem a **mesma** textura, num sinal de metade
da amplitude. Dividir pela média dentro da máscara — o mesmo truque que o ExG
normalizado já usa para a cor, um nível acima — levou a folhagem amarelada de
**5/24 para 22/24**.

**Ensaio na placa real, com nada ligado nela.** O firmware do vaso gravado no C3
subiu, entrou em modo AP, e a escada de recuperação da câmera funcionou como
projetada: falhas 1→6, risco `CÂMERA SEM RESPOSTA` publicado na terceira, pulso de
reset na sexta, `cam_resets: 1`. A bomba ficou bloqueada com
`"bomba de 12 V nao roda em USB"`, e o solo com o pino flutuando leu faixa
extremamente alta, que **também** bloqueia — os dois intertravamentos certos, pelos
dois motivos certos.

**Uma observação que não fecha:** o firmware imprimiu `[tela] OLED respondeu no
I2C` **sem nenhum OLED ligado**. Ou o `begin()` da biblioteca Adafruit não confere
o ACK como eu supus, ou o barramento flutuando produziu um falso positivo. Não
afeta nada hoje — sem display, o firmware desenha no vazio —, mas significa que a
mensagem não prova presença de display. Conferir com o OLED na mão.

**Decisão:** os pesos que vão para o firmware são os **restritos**, não os de maior
acerto. Perder 4 pontos no banco sintético compra a garantia de que o modelo não
inverteu o significado de "verde". Modelo com monotonicidade erra de forma
previsível; sem ela, erra de forma criativa.

**Decisão:** `BOMBA_EXIGE_PLANTA` fica em **0**. A visão avisa, não manda. Câmera
suja ou às escuras vira "não há planta", e o intertravamento deixaria a planta
secar por causa de uma lente empoeirada. Só ligar depois de medir falso negativo
com planta de verdade.

**Pendências, em ordem de importância:**

1. Nada disso passou por fio de verdade. O enlace foi exercitado em memória, na
   mesma placa. Taxa de erro real, comprimento de jumper e a câmera reiniciando de
   fato são ensaio de bancada.
2. Os limiares de solo e tanque continuam sendo chute educado, e agora com um
   agravante: foram herdados da DevKit V1, e **o ADC do C3 tem curva de atenuação
   diferente**. Calibrar na placa nova.
3. O classificador nunca viu uma foto.

**Evidência:** saída de `pio run` nos cinco ambientes; saída serial do `autoteste`
nas quatro iterações; saída serial do firmware `c3` gravado na placa.

---

### Próxima entrada esperada — Primeira gravação da câmera e enlace por fio

**Alvo previsto:** gravar a ESP32-CAM, ligar os quatro fios do enlace e medir o
que só o fio mede: quantos quadros de mil chegam com CRC errado, quanto tempo a
câmera leva de fato entre `PEDE_VEREDITO` e `VEREDITO`, e se o pulso de reset
recupera uma câmera travada de verdade.

**Previsão a registrar antes do ensaio:** com jumper de 10 cm a 115200 bps, a taxa
de erro deve ser indistinguível de zero — menos de 1 quadro em 10 000. Se aparecer
erro acima disso, a suspeita é terra comum mal feita antes de ser ruído.

**Segundo alvo:** apontar a câmera para uma planta de verdade e para um objeto
verde de plástico, e comparar a probabilidade dos dois com o que o banco sintético
previu. É esse número que diz se os pesos treinados valem alguma coisa fora do
gerador.

---

### Entrada antiga — Primeira gravação e calibração dos sensores

**Alvo previsto:** gravar numa DevKit V1 e medir os limiares reais de solo (no ar
e em terra encharcada) e de tanque (vazio e cheio), pelo ambiente `bancada`.

**Previsão a registrar antes do ensaio:** o sensor capacitivo alimentado em 3V3
deve entregar algo em torno de 2900–3100 no ar e 1100–1300 em terra saturada. Se
a faixa medida for muito mais estreita que isso, a suspeita é alimentação em 5 V
com divisor, ou sensor com verniz danificado.
