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

### Próxima entrada esperada — Primeira gravação e calibração

**Alvo previsto:** gravar numa DevKit V1 e medir os limiares reais de solo (no ar
e em terra encharcada) e de tanque (vazio e cheio), pelo ambiente `bancada`.

**Previsão a registrar antes do ensaio:** o sensor capacitivo alimentado em 3V3
deve entregar algo em torno de 2900–3100 no ar e 1100–1300 em terra saturada. Se
a faixa medida for muito mais estreita que isso, a suspeita é alimentação em 5 V
com divisor, ou sensor com verniz danificado.
