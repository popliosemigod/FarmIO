# Reconhecimento de planta

> **Atualizado em 29/09/2026:** o app já tinha "Tirar foto"; ganhou também
> "Baixar" — Henrique decidiu usar sempre o celular (um Galaxy A14) para os
> testes, e pediu que fotos e vídeos cheguem na galeria do aparelho.
> `download` no link já faz isso, sem precisar de app nem de integração
> nenhuma: o Android salva na pasta Download, que a galeria de fábrica indexa
> sozinha.

O vaso já sabia responder "há planta na frente ou não" (docs/05, o
classificador de dez características). O que faltava — pedido em 29/09/2026 —
era **qual** planta, e se ela está doente. Este documento é sobre
[`scripts/reconhece_planta.py`](../scripts/reconhece_planta.py), que resolve
isso sem tocar em `lib/farmio_visao/`.

## Os dois classificadores, e por que não é um só

| | `lib/farmio_visao/` (na placa) | `scripts/reconhece_planta.py` (no PC) |
| --- | --- | --- |
| Pergunta | há planta na frente? | qual planta, e como está? |
| Quando roda | a cada 10 s, sempre | só quando o app pede uma foto |
| Onde | XIAO ESP32-S3, embarcado | PC, via rede |
| Custo | 11,5 ms, alguns kB | ~2 s de foto pelo fio + inferência |
| Categorias | 2 (tem / não tem) | 38, treinadas no PlantVillage |
| Como decide | regressão logística de dez características, ponto fixo | rede convolucional (MobileNetV2), pré-treinada |

Fundir os dois seria caro à toa: rodar uma rede convolucional a cada 10 s na
XIAO gastaria energia e ainda não caberia — o próprio docs/05 explica por que
o classificador de bordo é deliberadamente pequeno. A resposta cara só faz
sentido quando alguém já pediu a foto pelo app, que é exatamente quando esse
custo se paga.

## O modelo

[`linkanjarad/mobilenet_v2_1.0_224-plant-disease-identification`](https://huggingface.co/linkanjarad/mobilenet_v2_1.0_224-plant-disease-identification)
— MobileNetV2 ajustado no **PlantVillage** (38 classes de cultura×estado:
"Healthy Tomato Plant", "Potato with Early Blight" etc.), baixado uma vez
(~14 MB) e cacheado pelo `huggingface_hub`. Achado pesquisando "plant disease
classifier huggingface transformers" em 29/09/2026, entre vários candidatos —
este foi escolhido por já vir com `id2label` legível em inglês, sem precisar
de tabela de tradução à parte.

**Licença "other" no card do modelo** — não auditada. Serve para triagem e
para aprender o formato do pipeline; não é uma garantia para decisão de
produção sobre a lavoura.

## Uso

```powershell
python -m pip install --user transformers pillow requests
python -m pip install --user --index-url https://download.pytorch.org/whl/cpu torch

python scripts/reconhece_planta.py                        # rede propria: 192.168.4.1
python scripts/reconhece_planta.py --host farmio-01.local # pelo roteador do celular
```

Cada chamada:

1. `POST /foto`, e espera `GET /foto/estado` responder `"pronta"` — os mesmos
   dois passos que a página do app já faz (ver `web.h`);
2. baixa `/foto.jpg` e salva em
   `evidencias/reconhecimento/AAAAMMDD-HHMMSS.jpg` (gitignorado, mesma regra
   de sempre para evidência pesada e volátil);
3. classifica e imprime as `--top` classes mais prováveis (padrão 5);
4. acrescenta uma linha a `evidencias/reconhecimento/catalogo.csv` — o começo
   do banco de fotos rotuladas do vaso.

## Um detalhe de biblioteca que valeu a pena documentar

A forma óbvia de usar este modelo é `transformers.pipeline("image-classification",
model=...)`. **Ela falha** nesta versão do `transformers` (5.17.0, 29/09/2026)
com `ValueError: Unrecognized image processor`: o
`preprocessor_config.json` do repositório declara
`"image_processor_type": "MobileNetV2FeatureExtractor"`, um nome de classe que
o `transformers` mais novo não reconhece mais.

`classifica()` não usa `pipeline()`: carrega o modelo com
`AutoModelForImageClassification` e faz o pré-processamento à mão, com os
MESMOS números que estão naquele arquivo (redimensiona pelo lado menor a 256,
corta o centro em 224×224, normaliza com média/desvio 0,5 nos três canais) —
o card do modelo, só que sem depender da classe que sumiu da biblioteca. Se
uma versão futura do `transformers` reintroduzir o nome, o `pipeline()` volta
a funcionar, mas não há necessidade de trocar: o caminho manual não depende
dessa resolução automática nunca mais.

## Smoke test (29/09/2026)

Rodado contra uma foto real já salva no repositório
(`evidencias/2026-09-21-xiao/03-q18-com-ajuste-ov3660.jpg`), sem o vaso
ligado — a mesma foto que docs/05 usa para ilustrar o falso positivo do
classificador de bordo:

```
43.6%  Tomato with Late Blight
15.7%  Bell Pepper with Bacterial Spot
11.7%  Tomato with Early Blight
 5.5%  Peach with Bacterial Spot
 3.4%  Cedar Apple Rust
```

A foto é de uma impressora 3D, não de uma planta — então este resultado é
**exatamente o mesmo tipo de falso positivo que docs/05 já documentou** para o
classificador de bordo, só que no classificador novo: as 38 classes do
PlantVillage são todas de folha de cultivo, e o modelo é forçado a escolher
uma mesmo diante de uma cena que não é nenhuma delas. O que este teste prova é
o **caminho de código** (baixar o modelo, pré-processar, classificar) — não a
qualidade do reconhecimento, que só se mede com foto real de planta.

## O que ainda não está provado

- **Nunca rodou contra o vaso ligado.** O caminho HTTP (`POST /foto` →
  `GET /foto/estado` → `GET /foto.jpg`) segue exatamente o que `web.h` já
  implementa e o app do celular já usa todo dia — está dimensionado, não
  medido por este script.
- **Nenhuma foto de planta de verdade passou por aqui ainda** — só a foto de
  bancada que também aparece no falso positivo do docs/05. A primeira medida
  de qualidade de verdade só existe depois que o vaso tiver uma planta na
  frente.
- Este classificador e o de bordo **nunca foram comparados na mesma foto com
  planta real** — quando isso acontecer, vale registrar os dois vereditos
  lado a lado no diário.
