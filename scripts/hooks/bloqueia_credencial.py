#!/usr/bin/env python3
"""Impede que credencial real entre no git.

Faz duas verificacoes:

1. Caminho proibido - secrets.ini, secrets.h, chave privada. O .gitignore ja
   cobre, mas um "git add -f" distraido passaria por cima dele.

2. Valor vazado - le config/secrets.ini (arquivo local, fora do git) e procura
   os valores SECRETOS dentro dos arquivos que estao sendo commitados. Pega o
   caso perigoso de verdade: alguem colar a senha do Wi-Fi dentro de um .h, de
   um README ou de um script.

O que conta como secreto e decidido pelo NOME DA CHAVE, nao pelo valor. Isso
importa: SSID, IP e modelo de impressora estao no mesmo arquivo mas nao sao
segredo nenhum - SSID e transmitido em broadcast por qualquer roteador, e
"Bambu Lab A1" e nome de produto. Bloquear esses valores faria o hook reprovar
a propria documentacao do laboratorio, e hook que grita sem motivo e hook que
todo mundo aprende a ignorar.

O proprio hook nunca contem a senha: ele a le do arquivo local em tempo de
execucao. Se config/secrets.ini nao existir, so a verificacao 1 roda.
"""
from __future__ import annotations

import configparser
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SEGREDOS = RAIZ / "config" / "secrets.ini"

# Nome de arquivo que nunca pode ser versionado.
PROIBIDOS = ("secrets.ini", "secrets.h", "secrets.local.ini", "secrets.local.h")
SUFIXOS_PROIBIDOS = (".key", ".pem")

# Uma chave e secreta se o nome dela contiver um destes fragmentos.
CHAVES_SECRETAS = (
    "senha",
    "password",
    "pass",
    "token",
    "secret",
    "key",
    "code",
    "credential",
)

# Valor curto demais nao serve de assinatura: procurar "1234" em todo arquivo
# do repositorio so geraria falso positivo.
TAMANHO_MINIMO = 6

# Marcadores do arquivo de exemplo. Nao sao segredo.
PLACEHOLDERS = {
    "ssid_aqui",
    "senha_aqui",
    "preencher",
    "coloque_aqui",
    "configjaspa",
    "senha_ota_aqui",
}


def valores_secretos() -> list[tuple[str, str]]:
    """Devolve (secao.chave, valor) apenas do que e segredo de verdade."""
    if not SEGREDOS.exists():
        return []
    cfg = configparser.ConfigParser()
    cfg.read(SEGREDOS, encoding="utf-8")
    achados = []
    for secao in cfg.sections():
        for chave, valor in cfg.items(secao):
            if not any(frag in chave.lower() for frag in CHAVES_SECRETAS):
                continue
            valor = valor.strip()
            if len(valor) < TAMANHO_MINIMO:
                continue
            if valor.lower() in PLACEHOLDERS:
                continue
            if set(valor) <= {"0"}:  # 0000... dos modelos de LoRaWAN
                continue
            achados.append((f"{secao}.{chave}", valor))
    return achados


def main(argv: list[str]) -> int:
    erros: list[str] = []
    segredos = valores_secretos()

    for nome in argv:
        caminho = Path(nome)

        if caminho.name in PROIBIDOS or caminho.suffix in SUFIXOS_PROIBIDOS:
            erros.append(f"{nome}: arquivo de credencial nao pode ser versionado")
            continue

        # O arquivo de exemplo e o proprio hook citam nomes de chave, nunca
        # valores reais.
        if ".example." in caminho.name or caminho.name == Path(__file__).name:
            continue

        try:
            texto = caminho.read_text(encoding="utf-8", errors="ignore")
        except (OSError, IsADirectoryError):
            continue

        for etiqueta, valor in segredos:
            if valor in texto:
                erros.append(
                    f"{nome}: contem o valor de config/secrets.ini [{etiqueta}]. "
                    "Leia a credencial em tempo de execucao, nao a escreva no arquivo."
                )

    if erros:
        print("CREDENCIAL BLOQUEADA - o commit foi interrompido:\n")
        for erro in erros:
            print(f"  * {erro}")
        print(
            "\nComo resolver: mantenha o valor apenas em config/secrets.ini "
            "(ignorado pelo git) ou em um secrets.h local, e versione so o "
            "arquivo .example correspondente."
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
