#!/usr/bin/env python3
"""Pede uma foto ao vaso pela rede e diz QUAL planta é, e se está doente.

Por que existe, e por que não mexe em lib/farmio_visao/: o classificador que
já roda na placa (docs/05-visao-planta.md) responde só "há planta na frente
ou não" — de propósito: é regressão logística em ponto fixo, cabe em poucos
kB e roda embarcado. Ele NÃO reconhece espécie, e não tenta. Este script é
um segundo estágio, que só entra quando alguém pede uma foto pelo app: baixa
o JPEG que o vaso já serve em /foto.jpg e classifica com uma rede treinada no
PlantVillage — 38 classes de cultura×estado (ex. "Tomato___healthy",
"Potato___Early_blight"), baixadas uma vez da Hugging Face e cacheadas. Não
há treino aqui, é reconhecimento pronto — igual ao pedido em 29/09/2026 para
o Feijão com Farinha, adaptado ao contexto de planta.

Os dois classificadores continuam com papéis diferentes: o da placa decide
constantemente e de graça (11,5 ms, sem rede); este aqui responde uma
pergunta mais cara ("qual planta, e como ela está") só quando vale o custo
de uma foto pelo fio e uma chamada de rede.

Uso:

    python scripts/reconhece_planta.py                       # http://192.168.4.1
    python scripts/reconhece_planta.py --host farmio-01.local
    python scripts/reconhece_planta.py --host 192.168.1.42 --top 3

Precisa de transformers, torch, pillow e requests:

    python -m pip install --user transformers pillow requests
    python -m pip install --user --index-url https://download.pytorch.org/whl/cpu torch

A primeira execução baixa o modelo (~14 MB) da Hugging Face e o guarda em
cache (~/.cache/huggingface); as próximas rodam offline. O modelo é de
terceiros (linkanjarad/mobilenet_v2_1.0_224-plant-disease-identification,
licença "other" no card) — bom para triagem e para aprender o formato do
pipeline, não auditado para decisão de produção.
"""
import argparse
import csv
import datetime
import io
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
PASTA_SAIDA = RAIZ / "evidencias" / "reconhecimento"
CATALOGO = PASTA_SAIDA / "catalogo.csv"

MODELO_HF = "linkanjarad/mobilenet_v2_1.0_224-plant-disease-identification"


def pede_e_espera_foto(sessao, base, prazo_s=15):
    r = sessao.post(f"{base}/foto", timeout=5)
    dado = r.json()
    if not dado.get("ok"):
        sys.exit(f"o vaso recusou a foto: {dado.get('erro', '?')}")

    ate = time.time() + prazo_s
    while time.time() < ate:
        r = sessao.get(f"{base}/foto/estado", timeout=5)
        estado = r.json()
        if estado["estado"] == "pronta":
            return estado
        if estado["estado"] == "erro":
            sys.exit(f"a foto falhou no vaso: {estado.get('erro', '?')}")
        time.sleep(0.3)
    sys.exit(f"a foto não ficou pronta em {prazo_s}s — o vaso está no ar? confira /sensores")


def baixa_jpeg(sessao, base, numero):
    r = sessao.get(f"{base}/foto.jpg", params={"n": numero}, timeout=10)
    r.raise_for_status()
    return r.content


def classifica(jpeg_bytes, top_k):
    # Import tardio: carregar transformers+torch custa alguns segundos, e só
    # vale pagar depois que a foto chegou de verdade.
    #
    # NÃO usa transformers.pipeline(): o preprocessor_config.json deste
    # repositório declara "image_processor_type": "MobileNetV2FeatureExtractor",
    # um nome de classe que o transformers 5.x não reconhece mais (a
    # resolução automática falha com "Unrecognized image processor"). Em vez
    # de brigar com a versão da biblioteca, o pré-processamento é feito à
    # mão, copiado dos MESMOS números que estão naquele arquivo: redimensiona
    # pelo lado menor a 256, corta o centro em 224x224, escala para 0..1 e
    # normaliza com média/desvio 0,5 nos três canais. É o card do modelo, só
    # que sem depender da classe que sumiu.
    import torch
    from PIL import Image
    from torchvision import transforms
    from transformers import AutoModelForImageClassification

    preprocessa = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(224),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.5, 0.5, 0.5], std=[0.5, 0.5, 0.5]),
    ])

    modelo = AutoModelForImageClassification.from_pretrained(MODELO_HF)
    modelo.eval()

    img = Image.open(io.BytesIO(jpeg_bytes)).convert("RGB")
    entrada = preprocessa(img).unsqueeze(0)

    with torch.no_grad():
        saida = modelo(pixel_values=entrada).logits[0]
    probs = torch.nn.functional.softmax(saida, dim=0)
    valores, indices = probs.topk(top_k)
    id2label = modelo.config.id2label
    return [(id2label[int(i)], float(v)) for v, i in zip(valores, indices)]


def salva_catalogo(caminho_jpg, largura, altura, previsoes):
    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    novo = not CATALOGO.exists()
    with open(CATALOGO, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if novo:
            w.writerow(["quando", "arquivo", "largura", "altura", "rotulo_1", "confianca_1", "rotulo_2", "confianca_2", "rotulo_3", "confianca_3"])
        linha = [datetime.datetime.now().isoformat(timespec="seconds"), caminho_jpg.name, largura, altura]
        for rotulo, conf in previsoes[:3]:
            linha += [rotulo, f"{conf:.4f}"]
        while len(linha) < 10:
            linha += ["", ""]
        w.writerow(linha)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1", help="endereço do vaso (padrão: a rede própria dele)")
    ap.add_argument("--top", type=int, default=5, help="quantas classes mostrar (padrão 5)")
    args = ap.parse_args()

    import requests

    base = f"http://{args.host}"
    sessao = requests.Session()

    print(f"[reconhece_planta] pedindo foto a {base}...")
    estado = pede_e_espera_foto(sessao, base)
    largura, altura = estado["largura"], estado["altura"]
    print(f"[reconhece_planta] pronta: {largura}x{altura}, {estado['total']} bytes em {estado['ms']} ms")

    jpeg_bytes = baixa_jpeg(sessao, base, estado["numero"])

    PASTA_SAIDA.mkdir(parents=True, exist_ok=True)
    nome = f"{datetime.datetime.now().strftime('%Y%m%d-%H%M%S')}.jpg"
    caminho = PASTA_SAIDA / nome
    caminho.write_bytes(jpeg_bytes)
    print(f"[reconhece_planta] salva em {caminho}")

    print("[reconhece_planta] classificando (primeira vez baixa o modelo, ~14 MB)...")
    previsoes = classifica(jpeg_bytes, args.top)
    for rotulo, conf in previsoes:
        print(f"    {conf * 100:5.1f}%  {rotulo}")

    salva_catalogo(caminho, largura, altura, previsoes)
    print(f"[reconhece_planta] registrado em {CATALOGO}")


if __name__ == "__main__":
    main()
