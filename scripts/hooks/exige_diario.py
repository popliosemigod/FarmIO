#!/usr/bin/env python3
"""Garante que o repositorio tenha README.md e diario.md.

O metodo do laboratorio depende do registro: previsao teorica antes do ensaio,
medida depois, divergencia anotada. Um projeto com firmware e sem diario perde
justamente a divergencia - que e a informacao que corrige o modelo teorico da
proxima iteracao. Sem ela, o proximo ensaio repete o erro que ja tinhamos
entendido.
"""
from __future__ import annotations

from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
OBRIGATORIOS = ("README.md", "diario.md")


def main() -> int:
    faltando = [nome for nome in OBRIGATORIOS if not (RAIZ / nome).exists()]
    if faltando:
        print("REGISTRO INCOMPLETO - o repositorio precisa destes arquivos:\n")
        for nome in faltando:
            print(f"  * falta {nome}")
        print("\nO diario guarda cada iteracao no formato "
              "alvo / previsao / medido / divergencia / decisao.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
