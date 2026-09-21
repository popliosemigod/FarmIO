#!/usr/bin/env python3
"""Mede o sensor de solo pela serial do vaso e espera a leitura ESTABILIZAR.

Por que existe: o sensor capacitivo leva minutos para assentar depois de
cravado na terra, e sobe ou desce se alguem mexe nele. Em 21/09/2026 a mesma
terra seca leu 1249, 2258, 2453 e 1738 em sequencia. Anotar o primeiro
numero que parece estavel calibra o vaso com um valor que nao se sustenta.

O criterio aqui: uma janela de N segundos em que maximo - minimo fica abaixo
da tolerancia. So entao o numero vale.

Uso (o vaso tem que estar rodando o firmware normal, no USB):

    python scripts/calibra_solo.py                  # mede e imprime a mediana
    python scripts/calibra_solo.py --porta COM10
    python scripts/calibra_solo.py --seco 1738 --molhado 950
        # com os dois pontos medidos, imprime os quatro limiares do config.h

Precisa de pyserial. O Python do PlatformIO ja traz:
    ~/.platformio/penv/Scripts/python.exe scripts/calibra_solo.py
"""
import argparse
import re
import statistics
import sys
import time

try:
    import serial
    import serial.tools.list_ports as listaportas
except ImportError:
    sys.exit("falta pyserial: use o Python do PlatformIO (~/.platformio/penv) ou pip install pyserial")


def acha_porta():
    # O vaso e o C3 (USB nativo, VID 303A). Se houver mais de uma placa
    # assim, pede a porta - errar de placa e medir a camera.
    cands = [p.device for p in listaportas.comports() if p.vid == 0x303A]
    if len(cands) == 1:
        return cands[0]
    sys.exit("achei %d portas 303A (%s); diga qual com --porta" % (len(cands), ", ".join(cands)))


def limiares(seco, molhado):
    """As mesmas regras dos valores de partida: 0% / 25% / 75% / 100% da faixa."""
    if seco <= molhado:
        sys.exit("o ponto seco (%d) tem que ser MAIOR que o molhado (%d): leitura alta = seco" % (seco, molhado))
    faixa = seco - molhado
    return {
        "SOLO_SECO_ADC": seco,
        "SOLO_BAIXO_ADC": round(seco - 0.25 * faixa),
        "SOLO_ALTO_ADC": round(seco - 0.75 * faixa),
        "SOLO_ENCHARCADO_ADC": molhado,
    }


def mede(porta, janela, tolerancia, limite):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = porta, 115200, 0.3
    s.dtr = s.rts = False
    s.open()
    s.write(b"j")  # o vaso passa a imprimir o JSON de /sensores
    buf, pontos, ini = "", [], time.time()
    print("lendo %s; esperando %d s com variacao < %d..." % (porta, janela, tolerancia))
    ultimo_aviso = 0
    while time.time() - ini < limite:
        buf += s.read(4096).decode("utf-8", "replace")
        pontos = [
            (int(t), int(v))
            for t, v in re.findall(r'"uptime_s":(\d+).*?"solo_adc":(\d+)', buf)
        ]
        if not pontos:
            continue
        agora = pontos[-1][0]
        recentes = [v for t, v in pontos if t >= agora - janela]
        cobre = agora - min(t for t, v in pontos if t >= agora - janela)
        if cobre >= janela - 3 and len(recentes) >= 8 and max(recentes) - min(recentes) < tolerancia:
            s.write(b"b")
            s.close()
            return recentes
        if time.time() - ultimo_aviso > 15:
            ultimo_aviso = time.time()
            print("  agora %d   (janela: %d a %d)" % (pontos[-1][1], min(recentes), max(recentes)))
    s.write(b"b")
    s.close()
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta")
    ap.add_argument("--janela", type=int, default=60, help="segundos de estabilidade exigidos (60)")
    ap.add_argument("--tolerancia", type=int, default=40, help="variacao maxima na janela, em ADC (40)")
    ap.add_argument("--limite", type=int, default=600, help="desiste depois de tantos segundos (600)")
    ap.add_argument("--seco", type=int, help="ponto seco ja medido: imprime os limiares")
    ap.add_argument("--molhado", type=int, help="ponto molhado ja medido")
    a = ap.parse_args()

    if a.seco is not None and a.molhado is not None:
        for k, v in limiares(a.seco, a.molhado).items():
            print("#define %-20s %d" % (k, v))
        return

    v = mede(a.porta or acha_porta(), a.janela, a.tolerancia, a.limite)
    if v is None:
        sys.exit("NAO estabilizou em %d s. Sensor firme na terra? Alguem mexendo nele?" % a.limite)
    print("\nESTAVEL: mediana %d   (minimo %d, maximo %d, %d leituras em %d s)"
          % (statistics.median(v), min(v), max(v), len(v), a.janela))
    print("Anote este numero como o ponto desta condicao (seco ou molhado).")


if __name__ == "__main__":
    main()
