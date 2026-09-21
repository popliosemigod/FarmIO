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

### 2026-09-21 (tarde) — A XIAO substitui a ESP32-CAM, e o classificador encontra o mundo real

**Alvo:** trocar a câmera da ESP32-CAM AI-Thinker pela Seeed XIAO ESP32-S3 Sense,
com o C3 continuando como vaso, e deixar o conjunto confiável para montar e levar
a campo. Ainda não há planta na bancada.

**Identificação da placa, antes de qualquer código.** Apareceu uma porta nova,
COM13, com VID 303A:1001, que é USB nativo da Espressif. Isso já descartava a
AI-Thinker, que só fala por um CH340. O `esptool flash_id` confirmou: **ESP32-S3
(QFN56) rev v0.2, PSRAM embutida de 8 MB, flash de 8 MB, MAC `…:DF:61:58`** — a
especificação exata da XIAO Sense. O histórico do Windows mostrou que este PC já
teve quatro CH340 e um FTDI com driver instalado; a ESP32-CAM nunca apareceu porque
o caminho físico dela não passava dado.

**O que foi feito:**

- **Pinagem da câmera por placa**, como já era a do controlador. `cam` passou a
  ser a XIAO; `cam-aithinker` continua compilando. O enlace na XIAO usa **D0/D1**,
  por eliminação: D6/D7 recebem o log de boot da ROM, D2 é strapping, D8–D10 são o
  SPI do cartão SD na placa Sense.
- **Watchdog do loop na câmera.** A XIAO não tem pino de reset na borda, então o
  degrau de reset da escada do vaso deixou de alcançá-la. Se o loop dela ficar 5 s
  sem voltar, o próprio chip reinicia.
- **Console de bancada na câmera** — `s` estado, `v` veredito, `F` foto para o PC.
  Foi o que permitiu testar a câmera sem o enlace, e ver com os próprios olhos o
  que ela vê.

**Medido na XIAO, pelo console USB:**

| | Previsto | Medido | |
| --- | --- | --- | --- |
| Sensor | OV2640 | **OV3660** | ✗ |
| Foto VGA, qualidade 14 | 20–30 kB | **49,9 kB** — de uma parede | ✗ |
| Foto VGA, qualidade 18 | — | **13,1–13,5 kB** | |
| Classificação por quadro | — | **206–210 ms** | |
| Veredito, cena **sem planta** | sem planta | **"planta", 1000‰, 3 de 3** | ✗✗ |

**Divergência 1 — o classificador errou no primeiro contato com o mundo real, e
com confiança máxima.** A cena: uma parede branca, um carretel de filamento, a
lateral escura de uma impressora 3D. Nenhuma planta. O veredito: planta, 1000‰,
três vezes, com 17–22% da cena contada como verde.

O banco sintético dava 97,9% de acerto. Este diário registrou em 09/09 que *"97,9%
num banco que eu mesmo gerei mede o gerador tanto quanto o modelo"* e que *"acerto
de campo é outra medida e ainda não existe"*. Agora ela existe: **uma cena real,
um erro, confiança total.**

A foto mostra a causa provável. O OV3660 pinta as áreas **escuras** de
verde-azulado. O ExG normalizado divide pela soma dos canais — é o que o torna
imune à luz —, e é essa mesma divisão que transforma um tom esverdeado leve, num
pixel escuro, em "muito verde". O gerador sintético nunca produziu esse defeito de
sensor, então o modelo nunca aprendeu a desconfiar dele.

Os ajustes de fábrica do OV3660 (vertical invertida, brilho +1, saturação −2)
acertaram a orientação e **não** removeram o erro.

**Divergência 2 — tamanho de foto duas vezes o estimado.** 49,9 kB contra 20–30 kB,
e de uma parede, que é a cena que comprime *melhor*. Uma planta tem mais detalhe e
comprimiria pior: o limite de 60 kB do vaso recusaria justamente a foto que o vaso
existe para tirar. Corrigido nos dois lados: qualidade 14 → 18 na câmera (13 kB,
~1,3 s de fio) e limite de 60 → 80 kB no vaso.

**Divergência 3 — o sensor.** Esperava-se OV2640; a XIAO desta remessa veio com
OV3660. O firmware detecta e aplica os ajustes certos para cada um.

**Decisão: a visão vai para campo marcada como não calibrada.** `VISAO_CALIBRADA`
entrou em 0. O app e a serial mostram o veredito com o aviso, e o alarme "nenhuma
planta à vista" fica desligado até a calibração. A visão já não comandava a bomba
(`BOMBA_EXIGE_PLANTA` = 0); agora também não alarma. **Não foi feito ajuste nenhum
no modelo** — com uma foto só, seria trocar um chute por outro.

**Verificado de novo, com sensor real:** o sensor de solo, que lia ~2910 de manhã,
à tarde estava solto e lendo 4095. O painel respondeu **"SEM LEITURA — FORA DA
FAIXA FÍSICA: sensor solto?"**. O firmware anterior a hoje teria respondido
"extremamente seco, irrigue".

**Estado para a montagem:**

| Peça | Estado |
| --- | --- |
| C3 (vaso) | gravado, lendo DHT22 (26,3 °C), rede própria no ar |
| XIAO (câmera) | gravada, OV3660 ok, foto e veredito funcionando pelo USB |
| Enlace C3 ↔ XIAO | **não ligado** — 94 perguntas sem resposta no vaso |
| Solo | solto (lê 4095) |
| Nível | seco ou solto (lê 3) — bloqueia a bomba, que é o lado seguro |
| Bomba | nunca girou |
| Classificador | **não calibrado**, com um falso positivo medido |

**Próxima entrada esperada:** os três fios do enlace — D0 da XIAO no GPIO20 do C3,
D1 no GPIO21, GND comum — e a primeira foto chegando no app pelo fio. Depois,
planta na bancada e as primeiras fotos reais com e sem planta, para o retreino.

**Evidência:** `evidencias/2026-09-21-xiao/` (três fotos: qualidade 14 sem ajuste,
qualidade 18 sem ajuste, qualidade 18 com ajuste do OV3660 — ficam fora do git, que
não versiona imagem), saída do console da XIAO e do painel do C3.

---

### 2026-09-21 — Foto pelo fio, botão da bomba, e o defeito que o bloqueio escondia

**Alvo:** quatro pedidos de uma vez. (1) Trocar o vídeo da câmera por uma foto sob
demanda. (2) Um botão no app que liga e desliga a bomba, sem influenciar a lógica
do vaso. (3) Preparar o projeto para campo aberto, onde a única rede é o roteador
de um celular (Galaxy A14). (4) Gravar as duas placas, que estariam ligadas ao PC.

**O que a bancada mostrou antes de qualquer código:** só o C3 enumerou. O
Gerenciador de Dispositivos não tinha adaptador USB-serial nenhum — nem CH340, nem
CP2102, nem FTDI, nem um sem driver. A ESP32-CAM não tem USB próprio; sem
adaptador no header de gravação, ela não é gravável deste PC. **O item 4 ficou
pela metade: o C3 foi gravado, a câmera não.**

**O defeito achado no caminho, e é o registro mais importante da entrada.** Para o
botão da bomba funcionar, o bloqueio de energia tinha de sair — a bomba agora tem
fonte própria de 7–9 V e a USB não paga mais a corrente dela. Antes de tirar,
conferi o que mais segurava a bomba. Nada segurava:

| Sensor solto, pino em 4095 | O firmware entendia | Efeito na bomba |
| --- | --- | --- |
| solo | "extremamente seco" | **pede irrigação** |
| nível | "100% cheio" | **libera a bomba** |

Os dois erros apontavam para o mesmo lado: bomba pulsando a seco a cada 24 s, por
causa de um fio. Desde 09/09 quem impedia isso era o bloqueio de energia — e só por
acaso, porque ele existia por outro motivo. Tirá-lo sem olhar teria exposto o
defeito no mesmo commit que entregava o botão.

**Correção:** leitura fora da faixa fisicamente possível não é leitura. O
capacitivo em 3V3 vive entre ADC ~1500 e ~3700; encostar em qualquer trilho é
sensor solto → `SOLO_INVALIDO`, que bloqueia. No nível, só o teto invalida —
encostar no chão é tanque vazio de verdade, e já bloqueia pelo outro lado. O limite
da regra está escrito em `docs/02`: ela pega pino encostado num trilho, que é o
que a bancada mostrou hoje; não pega pino flutuando no meio da faixa, que é o que
ela mostrou em 09/09.

**O que foi feito:**

- **Foto pelo fio (protocolo v2).** A câmera perdeu o Wi-Fi. O app pede ao vaso, o
  vaso pede pela UART, a câmera devolve o JPEG em pedaços de 196 bytes com
  deslocamento em cada um e CRC da imagem inteira no fim. Carga máxima do quadro
  subiu de 64 para 200 bytes; versão do protocolo de 1 para 2. VGA com escala 1/4
  no decodificador dá exatamente os 160×120 do classificador — um tamanho de quadro
  para os dois usos. De passagem, corrigido um defeito latente no ramo sem PSRAM
  (QQVGA com escala 1/2 lia 80×60 como se fosse 160×120).
- **Botão da bomba.** Camada manual separada do automático: contadores próprios,
  que o automático nunca lê; automático suspenso enquanto o app manda. O botão é
  um pedido com prazo — a página renova a cada 2 s, e sem renovação a bomba
  desliga em 6 s; teto de 30 s por acionamento. Os intertravamentos de hardware
  (energia, tanque vazio, tanque sem leitura) valem para o botão; os da planta
  (solo encharcado, solo sem leitura, teto do ciclo) valem só no automático.
- **Rede de campo.** Rede própria `farmio-01` sempre no ar em 192.168.4.1, mais o
  roteador do celular quando ele estiver ligado. Enquanto alguém usa a rede
  própria, o vaso não sai do canal para procurar o roteador. Credencial do
  roteador no `secrets.h`, que continua fora do repositório.
- **Senha da rede própria trocada.** A antiga, `farmio123`, está publicada no
  `secrets.example.h` de um repositório público — e desde hoje a rede dá acesso a
  um botão que liga a bomba. A nova vive só no `secrets.h`.

**Previsão × medido:**

| | Previsto | Medido | |
| --- | --- | --- | --- |
| Quadro de 207 B, varredura de bit único | 100% recusados | **1656/1656** | ✅ |
| Foto sintética de 5000 B remontada | byte a byte | **26 pedaços, CRC confere** | ✅ |
| Pedaço perdido | detectado | **detectado, não remendado** | ✅ |
| Flash da câmera sem Wi-Fi | cair | **474 → 360 kB (−114 kB)** | ✅ |
| Flash do vaso | ~68% | **68,2%** (894 kB) | ✅ |
| Compila sem `secrets.h` | sim | **sim** (`c3` e `cam`) | ✅ |
| Solo no ar, capacitivo em 3V3 | ADC 2900–3100 | **2900–2923** | ✅ se estava no ar |

**Primeiras leituras reais do projeto.** Com o C3 gravado, os sensores estavam
ligados e responderam: **DHT22 em 26,5 °C e 68,4% de umidade, zero falhas**; solo
em ADC ~2910; nível em ADC 32 → tanque vazio. A última linha da tabela acima é a
previsão anotada neste diário em 02/09, antes de qualquer sensor existir — e ela
bateu, **se** o sensor estava fora da terra. Não sei se estava. Fica registrado
como coincidência promissora, não como calibração.

**Os comandos de bancada responderam pelo motivo certo:**

- `f` → `foto recusada: camera sem enlace` — a câmera não roda o firmware do projeto;
- `+` → `bomba recusada: tanque vazio` — o botão não passa por cima da proteção
  contra bomba a seco, com leitura real de sensor.

**O que NÃO foi verificado, e precisa ficar claro:**

1. **Nenhuma rota HTTP nova foi exercitada.** O PC está em outra rede, e conectá-lo
   à `farmio-01` derrubaria a conexão dele. As serial `f` e `+` passam pelas mesmas
   funções do app (`Camera::pedeFoto`, `Bomba::ligaManual`), então o núcleo está
   verificado — a página, o JavaScript e as rotas, não.
2. **Nenhuma foto real foi tirada.** A câmera não foi gravada.
3. **A bomba não girou.** Todo acionamento foi recusado pelo tanque vazio, que é o
   comportamento correto e o único que deu para ver.

**Decisão que precisa de validação em campo:** como o automático não enxerga o
manual, ele pode disparar um pulso logo depois de uma rega pelo app, se o sensor
ainda não sentiu a água. Não está tratado porque o pedido foi, explicitamente, não
mexer na lógica. Registrado em `docs/03` com a linha que resolveria.

**Evidência:** saída serial do `autoteste` (seções 1 e 1b) e do `c3`, gravados na
COM10; `pio run` nos seis ambientes, com e sem `secrets.h`.

---

### 2026-09-18 — O driver da bomba não é o que estava escrito

**Alvo:** documentar a pinagem da ponte H. Virou outra coisa no meio.

**O que aconteceu:** ao responder quantos pinos a ponte H ocupa, respondi pela
pinagem do **TB6612FNG**, que é o que estava escrito em todo o repositório desde a
v0.1. Henrique corrigiu: o driver da bancada é um módulo mini, e ao conferir o
header ele relatou **`IN1` a `IN4`, `GND`, sem pinos de enable, e tensão de até
11 V**.

**O número que importa é o teto de 11 V.** Ele não é uma diferença de pinagem — é
uma contradição com a bomba do projeto:

| | Especificação |
| --- | --- |
| Driver na bancada | até **11 V** |
| Bomba RS-385 | **12 V** nominais |

E ele também diz que o chip **não é um L298N**, apesar de o módulo ser vendido com
esse nome: o L298N aceita 46 V na saída de potência. Pelo teto de tensão, as
candidatas prováveis são DRV8833 (2,7–10,8 V, 1,5 A por canal) e L9110S/HG7881
(2,5–12 V, 800 mA por canal).

**A contagem de pinos não mudou, mas a margem de segurança sim.** Continua um pino
— GPIO3, agora no `IN1`, com `IN2` no GND. O que sumiu foi o **STBY**: com o
TB6612FNG havia dois mecanismos garantindo bomba parada durante o boot, o STBY em
pull-down e o duty zero. Este módulo não tem enable nem STBY. **O pull-down de
10 kΩ deixou de ser reforço e passou a ser o único cadeado** — se faltar na solda,
o pino do C3 fica em alta impedância durante o boot inteiro e não há nada atrás.

**Corrigido no firmware:** `BOMBA_PWM_FREQ` caiu de 20 000 para 1 000. Os 20 kHz
vinham do TB6612FNG, que é MOSFET; qualquer uma das candidatas atuais prefere
frequência baixa, e num Darlington 20 kHz seria forno. Na prática o ruído audível
não volta, porque a bomba só é acionada em duty 100%, onde não há chaveamento
nenhum — mas o número estava errado para o hardware real.

**Divergência de método, e é a lição da entrada:** o repositório afirmava
`TB6612FNG` em seis lugares — `config.h`, `bomba.h`, `main.cpp`, o README e duas
seções de `docs/02`. Nenhum deles era medida; todos eram a mesma suposição
propagada da v0.1, que ganhou aparência de fato por repetição. Um componente que o
firmware aciona mas que ninguém conferiu na bancada é exatamente o tipo de número
que o diário existe para marcar como não verificado, e ele passou seis commits sem
essa marca.

**Decisão, tomada na mesma conversa:** a bomba passa a ser alimentada em **7 a
9 V**, com folga sob o teto do driver. Motor CC aceita subtensão sem drama — girar
devagar não danifica nada —, então o conflito se resolve sem trocar peça. O que
muda é a vazão: cerca de 75% da nominal em 9 V, pouco mais da metade em 7 V.

`BOMBA_DRIVER_VMAX_V` (11) e `BOMBA_TENSAO_V` (9) entraram no `config.h`.

**Dívida que essa decisão cria:** `BOMBA_PASSO_MS` vale 4 s porque foi dimensionado
para 12 V. Com menos vazão, o mesmo pulso leva menos água ao vaso, e o tempo certo
só sai do ensaio com planta. O parâmetro desce de "dimensionado" para "chute
educado", na mesma condição dos limiares de solo — e isso precisa estar escrito,
porque um número que já foi calculado não anuncia sozinho que deixou de valer.

**Próximo passo, e é de bancada:** ler a marcação impressa no chip do módulo. É ela
que decide entre 800 mA e 1,5 A. A decisão de 7–9 V ajuda também aqui, porque a
corrente de rotor travado escala com a tensão: os mais de 2 A da RS-385 em 12 V
viram ~1,5 A em 9 V e ~1,2 A em 7 V. O problema saiu de "certamente demais" para
"depende do chip" — um DRV8833 aguenta, um L9110S fica no limite.

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
