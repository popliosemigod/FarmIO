# FarmIO

**Vaso inteligente e automático.** Irriga sozinho, enxerga se há planta na frente,
avisa o que está errado e mostra tudo em três lugares: no display, na página web e
na serial.

> **Estado: compila, está dimensionado, e o firmware do vaso já rodou numa placa
> real.** O que ainda não existe é ensaio com sensor, com bomba e com a câmera
> apontada para alguma coisa. Os limiares dos sensores são ponto de partida, não
> medida — ver [calibração](docs/02-hardware-e-pinagem.md#calibração--leia-antes-de-confiar-em-qualquer-leitura).

Este é um projeto do laboratório [**Jaspy**](https://github.com/popliosemigod/Jaspy),
que guarda o método, as ferramentas e a documentação comum. Cada projeto tem
repositório próprio; este é o do FarmIO.

## O que ele faz

- lê temperatura e umidade do ar (DHT22), umidade do solo e nível do tanque;
- **irriga em pulsos** quando o solo fica extremamente seco, e para na hora se o
  tanque esvaziar ou o solo encharcar;
- classifica o solo em cinco faixas em vez de um limiar único — limiar único faz a
  bomba oscilar em torno do ponto de corte;
- **reconhece se há uma planta na frente da câmera**, por textura e não por cor —
  é o que impede um balde verde de passar por planta;
- **conhece o próprio orçamento de corrente** e ajusta o brilho do anel ao que a
  porta USB aguenta, em vez de descobrir o limite reiniciando;
- alerta em três canais: tela, anel de LED e página web;
- transmite vídeo ao vivo, embutido na mesma página — e acha o endereço da câmera
  sozinho.

## Duas placas

| Papel | Placa | O que roda nela |
| --- | --- | --- |
| O vaso | **ESP32-C3** (4 MB, USB nativo) | sensores, bomba, tela, anel, página |
| O olho | **ESP32-CAM** AI-Thinker | captura, classificação, vídeo |

Elas conversam por **UART**, não por Wi-Fi — as razões estão em
[04-enlace-c3-cam.md](docs/04-enlace-c3-cam.md). Pelo fio passam doze bytes: o
veredito. O vídeo continua indo por rádio, que é o que fio de 115200 bps não
aguenta.

| Item | Componente |
| --- | --- |
| Ar | DHT22 |
| Solo | sensor capacitivo de umidade |
| Tanque | sensor de nível tipo pente (Funduino) |
| Tela | OLED SSD1306 128×64, I²C |
| Luz | anel de 16 LEDs WS2812 (5050) |
| Bomba | RS-385 12 V via TB6612FNG, **um pino de controle** |

Pinagem completa, alimentação e as armadilhas de ADC e strapping estão em
[`docs/02-hardware-e-pinagem.md`](docs/02-hardware-e-pinagem.md).

## Compilar e gravar

```powershell
pio run                        # compila o vaso e a câmera
pio run -e c3        -t upload # grava o vaso (USB nativo)
pio run -e cam       -t upload # grava a câmera (adaptador USB-TTL, GPIO0 no GND)
pio run -e autoteste -t upload # treina e mede, sem nada ligado na placa
pio run -e bancada   -t upload # o vaso com log detalhado, para calibrar
pio device monitor -e c3
```

| Ambiente | Placa | Para que serve |
| --- | --- | --- |
| `c3` | ESP32-C3 | o vaso |
| `cam` | ESP32-CAM | a câmera |
| `autoteste` | ESP32-C3 | exercita o enlace e treina o classificador na placa |
| `bancada` | ESP32-C3 | o vaso com log detalhado |
| `esp32dev` | DevKit V1 | a placa da v0.1, mantida compilando |

Credencial de Wi-Fi é opcional para compilar:

```powershell
Copy-Item include\secrets.example.h include\secrets.h   # e preencher
```

Sem `secrets.h` o firmware compila e roda — o vaso sobe o próprio ponto de acesso
(`farmio-01`) e espera configuração. É isso que permite o CI compilar sem nenhuma
senha. **A detecção de planta não depende de rede nenhuma**: ela vive no fio.

## Consumo de recursos

Compilação de 09/09/2026:

| Ambiente | RAM | Flash |
| --- | --- | --- |
| `c3` (vaso) | 16,8% — 54,9 kB de 320 kB | 65,0% — 852 kB de 1,31 MB |
| `cam` (câmera) | 14,4% — 47,2 kB de 320 kB | 15,1% — 474 kB de 3,15 MB |

## Estrutura

```
FarmIO/
├── platformio.ini      cinco ambientes, bibliotecas fixadas, build flags
├── include/
│   ├── config.h        pinagem, limiares, orçamento  ← só isso muda ao trocar de placa
│   ├── sensores.h      DHT22, solo, tanque, filtro de mediana
│   ├── bomba.h         acionamento e intertravamentos
│   ├── camera.h        o lado vaso do enlace, e a escada de recuperação
│   ├── energia.h       orçamento de corrente e teto de brilho
│   ├── anel.h          anel WS2812, animações não bloqueantes
│   ├── tela.h          OLED, tela inicial e tela de risco
│   ├── web.h           servidor HTTP, JSON e vídeo
│   └── cenas.h         gerador de cenas sintéticas (só no autoteste)
├── lib/
│   ├── farmio_enlace/  protocolo de quadros — C++11 puro, os dois lados do fio
│   └── farmio_visao/   dez características + regressão logística em ponto fixo
│       └── pesos.cpp   o modelo: onze números, gerados pelo autoteste
├── src/
│   ├── main.cpp            o vaso
│   ├── main_cam.cpp        a câmera
│   ├── main_autoteste.cpp  treino e medição na placa
│   └── cenas.cpp           desenho das cenas sintéticas
├── docs/               herança, hardware, lógica, enlace, visão, energia
└── diario.md           previsto × medido, a cada iteração
```

## Documentação

- [Herança do SmartFarm](docs/01-heranca-smartfarm.md) — de onde o projeto veio, a
  especificação original preservada e o que mudou
- [Hardware e pinagem](docs/02-hardware-e-pinagem.md) — ligação, ADC, strapping, os
  treze pinos do C3 e o roteiro de calibração
- [Lógica de operação](docs/03-logica-de-operacao.md) — faixas, intertravamentos,
  riscos, telas e anel
- [Enlace C3 ↔ ESP32-CAM](docs/04-enlace-c3-cam.md) — por que fio e não Wi-Fi,
  formato do quadro, escada de recuperação
- [Visão: tem planta na frente?](docs/05-visao-planta.md) — as dez
  características, o treino na placa e as quatro iterações até o modelo atual
- [O projeto inteiro numa porta USB](docs/06-energia-usb.md) — o orçamento de
  corrente, o que cabe e o que não cabe
- [Diário](diario.md) — cada iteração com previsto ao lado de medido

## Próximos passos

1. **Ligar os quatro fios do enlace** e gravar a ESP32-CAM. É o que transforma "o
   protocolo passa em 152 de 152 ensaios de bit" em "o enlace funciona".
2. **Apontar a câmera para uma planta de verdade** e para um objeto verde de
   plástico. É esse número que diz se os pesos treinados valem alguma coisa fora
   do gerador de cenas.
3. **Calibrar solo e tanque** com o ambiente `bancada`, na placa nova — os
   limiares vieram da DevKit V1 e o ADC do C3 tem outra curva.
4. **Medir a corrente com amperímetro** e comparar com o `energia_ma` publicado no
   JSON.
5. **Ensaio de irrigação** com planta real, com fonte de 12 V ou bomba de 5 V.
6. **Acoplamento entre vasos**, que dá nome ao projeto. Nenhum protocolo definido
   ainda — mas o quadro do enlace já é o candidato natural.

## Convenções

Commits seguem `tipo(subsistema): descrição`, validados pelo `commitizen`
([`cz.toml`](cz.toml)). Branches: `main` guarda o estado coerente e publicado,
`develop` integra o trabalho em curso, `feat/<assunto>` para tarefa curta.

**Tag de versão só nasce de coisa medida.** A v0.2 está na `main` porque compila,
roda numa placa e tem número medido — mas não recebeu tag, porque nenhum sensor,
nenhuma bomba e nenhuma câmera foram ligados ainda.

```powershell
python -m pip install --user pre-commit commitizen
pre-commit install --install-hooks
pre-commit install --hook-type commit-msg
```
