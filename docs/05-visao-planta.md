# Visão: tem planta na frente?

O classificador mais simples que responde essa pergunta com honestidade — e o
caminho de tentativa e erro que levou até ele, incluindo os três modelos
descartados.

## O que é, e o que não é

Não é rede neural, não é TensorFlow Lite e não reconhece espécie. É um **extrator
de dez características seguido de uma regressão logística de onze números**. Cabe
em poucos kB, roda em 11,5 ms e — o que mais importa — cada número dele pode ser
explicado. Quando ele erra, dá para ver qual característica levou ao erro.

O código está em [`lib/farmio_visao/`](../lib/farmio_visao/) e é C++11 puro, sem
Arduino, sem alocação dinâmica e sem ponto flutuante.

## Por que não basta contar pixel verde

Esta é a armadilha inteira do problema. Um pano verde, um vaso de plástico verde
e uma parede pintada de verde têm **mais verde puro** que uma folha de verdade —
folha reflete pouco e tem sombra própria. Medido no banco de cenas:

| Cena | ExG médio | Desvio de textura |
| --- | --- | --- |
| Folhagem densa | 169 | 50 |
| Plástico verde | **228** | 14 |
| Pano verde liso | 160 | **0** |

A contagem de verde sozinha classifica o balde como planta. O que separa folhagem
de superfície verde lisa não é a cor, é a **irregularidade**: folha tem nervura,
borda recortada, sombra entre camadas e brilho especular em ponto.

Por isso, das dez características, cinco medem textura e forma, quatro medem cor
e uma mede qualidade do quadro.

## Os índices

**ExG normalizado** (excesso de verde):

```
ExG = 255 · (2G − R − B) / (R + G + B)
```

A divisão pela soma é o detalhe que faz o índice funcionar de manhã e de tarde:
cancela a intensidade da luz e deixa só a cromaticidade. Sem normalizar, a mesma
folha na sombra cai para metade do valor e o limiar fixo perde a planta
justamente quando o vaso está na sombra.

**Limiar de Otsu.** O corte entre "verde" e "fundo" sai do histograma de cada
quadro, escolhendo o valor que maximiza a separação entre as duas populações. Um
piso fixo (`VISAO_PISO_EXG`) continua existindo, porque numa cena inteira marrom
o Otsu ainda parte o histograma no meio e chamaria terra de folha.

## As dez características

| # | Nome | O que mede | Peso treinado |
| --- | --- | --- | --- |
| 0 | `cobertura` | fração de amostras acima do limiar | +0,55 |
| 1 | `exgMedio` | quão verde é o que passou | +0,38 |
| 2 | `exgDesvio` | desvio do ExG **relativo à média** — textura | **+3,06** |
| 3 | `bordas` | gradiente do ExG **relativo à média** — recorte | **+4,55** |
| 4 | `saturacao` | saturação média na máscara | 0 |
| 5 | `brilho` | brilho médio da cena | 0 (proibido) |
| 6 | `maiorRegiao` | maior aglomerado conexo | 0 |
| 7 | `clusters` | quantos aglomerados separados | +1,73 |
| 8 | `perimetro` | perímetro/área da máscara | 0 |
| 9 | `calor` | equilíbrio R × B na máscara | **+2,71** |

Ler os pesos diz o que o modelo aprendeu: **textura domina, cor entra pouco**.
Isso não é defeito — é o resultado que se queria. Um modelo em que a cor decidisse
chamaria o balde verde de planta.

Quatro pesos zerados significam que aquelas características não carregam
informação além do que as outras já dão. Ficam medidas mesmo assim: aparecem no
JSON e são o que permite diagnosticar um erro futuro.

## O ciclo de tentativa e erro

Não há uma única foto deste vaso — a câmera acabou de chegar. Sem dado, restariam
duas saídas: chutar os pesos e chamar de modelo, ou não entregar modelo nenhum. A
terceira saída foi gerar **cenas sintéticas** cujas estatísticas de cor e textura
são defensáveis, treinar em cima delas, e dizer com todas as letras o que isso
vale.

O banco tem 13 tipos de cena × 24 sementes. As dificeis existem de propósito:
**muda pequena** (positivo com 0,7% de cobertura), **folhagem amarelada**
(positivo clorótico), **pano verde** e **plástico verde** (negativos mais verdes
que qualquer planta). As 16 primeiras sementes de cada tipo treinam; as 8 últimas
são reservadas para medir — misturar as duas daria um número bonito e mentiroso.

Tudo roda **na própria placa** (`pio run -e autoteste -t upload`). Treinar onde se
executa elimina de uma vez a classe de erro mais chata deste tipo de trabalho: o
modelo que acerta no notebook e erra no microcontrolador porque a extração de
característica divergiu entre as duas implementações.

### As quatro iterações

| # | Modelo | Reserva | O que o resultado revelou |
| --- | --- | --- | --- |
| 1 | Pesos chutados à mão, 9 caract. | 70,8% | ponto de partida |
| 2 | Treino livre, 9 caract. | 95,8% | **peso negativo em `exgMedio`** |
| 3 | Treino com restrição de sinal, 9 caract. | 91,7% | **peso −2,77 em `brilho`** |
| 4 | + `calor`, + textura relativa, 10 caract. | **97,9%** | zero falso negativo |

**Iteração 2 — o modelo que acertava pelo motivo errado.** 95,8% de acerto com
peso **negativo** em `exgMedio` e em `perimetro`. Ou seja: aprendeu que "verde
demais é suspeito", porque no banco os negativos difíceis são os mais verdes de
todos. Dentro do banco isso é verdade e dá acerto alto; fora dele é falso, e
derrubaria a primeira planta viçosa no sol.

A resposta não foi treinar mais — foi **proibir**. Cada característica ganhou um
sinal permitido, vindo da física e não dos dados, e a descida de gradiente é
projetada de volta nesse semiespaço a cada passo. Custou 4,1 pontos e comprou
monotonicidade: mais verde nunca pode *diminuir* a chance de haver planta, mais
saturado nunca pode aumentar. Um modelo com essa garantia erra de forma
previsível; sem ela, erra de forma criativa.

**Iteração 3 — o segundo atalho.** Com os sinais impostos, o treino jogou peso
−2,77 em `brilho`: "cena escura, provavelmente planta". No banco isso também é
verdade — a única cena escura positiva é folhagem na sombra, e as claras (parede,
céu, solo seco) são todas negativas. É atalho do gerador, não fato do mundo:
planta ao sol é clara. O `brilho` passou a ser **proibido** de entrar na decisão.
Ele continua medido e continua servindo para a flag de luz baixa — que é
julgamento sobre a *qualidade* do quadro, não sobre o conteúdo.

**Iteração 4 — as duas correções que fecharam a conta.**

A primeira foi a característica `calor`, o equilíbrio entre vermelho e azul dentro
da máscara. Ela vem da física do pigmento: a clorofila absorve o azul com mais
força que o vermelho, então vegetação — viva ou senescente — reflete mais vermelho
que azul. Corante verde de plástico e de tecido é ciano-deslocado e faz o
contrário.

A segunda foi maior. Com `bordas` e `exgDesvio` em unidades **absolutas** de ExG,
a folhagem amarelada acertava 5 de 24. O motivo não era a cor dela ser esquisita —
era que o ExG de folha clorótica vale ~84 contra ~168 de folha sadia, e toda a
textura medida em cima dele encolhe na mesma proporção. O classificador lia "folha
amarelada tem metade da textura de uma folha", que é falso: ela tem a **mesma**
textura, num sinal de metade da amplitude.

Dividir pela média dentro da máscara torna as duas medidas invariantes a escala —
exatamente o que a divisão pela soma dos canais já fazia com a cor, um nível
acima. Depois disso, folha sadia e folha clorótica dão o mesmo número de bordas
(~72), e plástico verde continua em ~20.

Folhagem amarelada foi de **5/24 para 22/24**, e o modelo restrito passou a bater
o livre das iterações anteriores.

### O resultado medido

Ensaio de 09/09/2026, ESP32-C3, conjunto reservado (96 amostras nunca vistas no
treino):

| | |
| --- | --- |
| Acerto na reserva | **97,9%** (94/96) |
| Falso negativo | **0** |
| Falso positivo | 2 |
| Tempo de extração | **11,5 ms** por quadro |

Por cena, com a probabilidade média:

| Cena | Acerto | Prob. média |
| --- | --- | --- |
| folhagem densa | 22/24 | 753‰ |
| folhagem esparsa | 24/24 | 937‰ |
| muda pequena | 24/24 | 909‰ |
| folhagem na sombra | 24/24 | 975‰ |
| folhagem amarelada | 22/24 | 799‰ |
| solo seco | 23/24 | 55‰ |
| solo úmido | 20/24 | 178‰ |
| bancada de madeira | 24/24 | 14‰ |
| parede branca | 24/24 | 14‰ |
| **pano verde liso** | 24/24 | 128‰ |
| **plástico verde** | 24/24 | 262‰ |
| céu pela janela | 24/24 | 14‰ |

## O que esses 97,9% valem — e o que não valem

**Acerto medido aqui não é acerto de campo.** Cena sintética não tem desfoque de
lente, nem ruído de sensor CMOS com pouca luz, nem o auto-ganho da OV2640 puxando
a cor toda para o cinza.

O que este banco entrega é mais modesto e mais útil: garante que o modelo aprendeu
a separar folhagem de superfície verde lisa **por textura**, e não decorou "verde
= planta" — que é o único erro que um classificador de dez características pode
cometer de forma irrecuperável. Os pesos que saíram daqui são ponto de partida
honesto, a ser recalibrado com foto real no primeiro ensaio com a câmera.

Enquanto isso não acontecer, **a visão avisa e não manda**: `BOMBA_EXIGE_PLANTA`
está em 0.

## Filtro temporal

Um quadro isolado não decide nada. Passar a mão na frente da câmera, uma nuvem ou
o auto-ganho fazem a probabilidade pular. O vaso guarda os últimos 8 vereditos e
usa histerese: sobe para "tem planta" com média ≥ 600‰, só desce abaixo de 400‰.

Quadro com falha de captura, luz baixa ou estouro **não entra no filtro** —
entraria como "sem planta" e derrubaria a média por um motivo que nada tem a ver
com haver ou não planta.

## Retreinar

```powershell
pio run -e autoteste -t upload
pio device monitor -e autoteste
```

O autoteste imprime, no fim, o bloco pronto para colar em
[`lib/farmio_visao/pesos.cpp`](../lib/farmio_visao/pesos.cpp). O arquivo existe
sozinho justamente para isso: retreinar mexe em um lugar só, e o diff de um
retreino é onze números.

A rodada leva cerca de sete minutos — o C3 não tem unidade de ponto flutuante, e a
geração das 312 cenas é o gargalo, não o treino.

**Ao mexer nas escalas de `normaliza()`, os pesos treinados perdem a validade.**
Elas são parte do modelo, não do pré-processamento.
