# FarmIO

**Vaso inteligente e automático.** Irriga sozinho, enxerga se há planta na frente,
avisa o que está errado e mostra tudo em três lugares: no display, na página web e
na serial.

> **Estado: o firmware do vaso roda numa placa real, com DHT22, solo e nível
> ligados e respondendo.** O que ainda não existe é ensaio com a bomba girando e
> com a câmera gravada com o firmware do projeto. Os limiares dos sensores são ponto de partida, não
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
- **tira uma foto quando o app pede** — pelo fio, sem a câmera precisar de rede;
- **liga e desliga a bomba por um botão no app**, sem mexer na lógica automática e
  sem passar por cima da proteção contra bomba a seco;
- **funciona em campo aberto só com um celular**: tem rede própria sempre no ar, e
  entra no roteador do celular quando ele estiver ligado.

## Duas placas

| Papel | Placa | O que roda nela |
| --- | --- | --- |
| O vaso | **ESP32-C3** (4 MB, USB nativo) | sensores, bomba, tela, anel, página |
| O olho | **ESP32-CAM** AI-Thinker | captura, classificação, vídeo |

Elas conversam por **UART**, não por Wi-Fi — as razões estão em
[04-enlace-c3-cam.md](docs/04-enlace-c3-cam.md). Pelo fio passam o veredito, a
cada 10 s, e a foto, quando o app pede. A câmera não tem rádio ligado: o celular
só conversa com o vaso.

| Item | Componente |
| --- | --- |
| Ar | DHT22 |
| Solo | sensor capacitivo de umidade |
| Tanque | sensor de nível tipo pente (Funduino) |
| Tela | OLED SSD1306 128×64, I²C |
| Luz | anel de 16 LEDs WS2812 (5050) |
| Bomba | RS-385 em 7–9 V, fonte própria, via ponte H dupla mini — **um pino** |

Pinagem completa, alimentação e as armadilhas de ADC e strapping estão em
[`docs/02-hardware-e-pinagem.md`](docs/02-hardware-e-pinagem.md).

## Compilar e gravar

```powershell
pio run                        # compila o vaso e a câmera
pio run -e c3        -t upload # grava o vaso (USB nativo)
pio run -e cam       -t upload # grava a câmera (adaptador USB-TTL, GPIO0 no GND)
pio run -e autoteste -t upload # treina e mede, sem nada ligado na placa
pio run -e ensaio    -t upload # le so o DHT22 e o nivel, sem mais nada
pio run -e bancada   -t upload # o vaso com log detalhado, para calibrar
pio device monitor -e c3
```

| Ambiente | Placa | Para que serve |
| --- | --- | --- |
| `c3` | ESP32-C3 | o vaso |
| `cam` | ESP32-CAM | a câmera |
| `autoteste` | ESP32-C3 | exercita o enlace e treina o classificador na placa |
| `ensaio` | ESP32-C3 | bring-up de sensor, um subsistema por vez |
| `bancada` | ESP32-C3 | o vaso com log detalhado |
| `esp32dev` | DevKit V1 | a placa da v0.1, mantida compilando |

Credencial de Wi-Fi é opcional para compilar:

```powershell
Copy-Item include\secrets.example.h include\secrets.h   # e preencher
```

Sem `secrets.h` o firmware compila e roda — só com a rede própria. É isso que
permite o CI compilar sem nenhuma senha. **A detecção de planta e a foto não
dependem de rede nenhuma**: elas vivem no fio.

### Em campo: como chegar no app

| Caminho | Como | Endereço |
| --- | --- | --- |
| **rede do vaso** — sempre funciona | no celular, entrar no Wi-Fi `farmio-01` | `http://192.168.4.1` |
| roteador do celular | ligar o roteador do celular; o vaso entra sozinho | o IP sai na serial; em alguns celulares, `http://farmio-01.local` |

O roteador do celular precisa estar em **2,4 GHz** e **WPA2** — as placas não
enxergam 5 GHz, e o erro que aparece é "rede não encontrada", não "senha errada".

## Consumo de recursos

Compilação de 21/09/2026:

| Ambiente | RAM | Flash |
| --- | --- | --- |
| `c3` (vaso) | 17,4% — 57,0 kB de 320 kB | 68,2% — 894 kB de 1,31 MB |
| `cam` (câmera) | 13,4% — 43,9 kB de 320 kB | 11,5% — 360 kB de 3,15 MB |

A câmera encolheu 114 kB em 21/09: é a pilha de Wi-Fi que saiu junto com o vídeo.

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
│   ├── web.h           o app: página, JSON, foto e botão da bomba
│   ├── telemetria.h    o painel legível da serial, e os comandos de bancada
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

1. **Gravar a ESP32-CAM.** Ela precisa de um adaptador USB-serial no header de
   gravação — não tem USB próprio. É o que destrava o enlace, o veredito e a foto
   de uma vez.
2. **Apontar a câmera para uma planta de verdade** e para um objeto verde de
   plástico. É esse número que diz se os pesos treinados valem alguma coisa fora
   do gerador de cenas.
3. **Calibrar solo e tanque** com o ambiente `bancada`, na placa nova — os
   limiares vieram da DevKit V1 e o ADC do C3 tem outra curva.
4. **Medir a corrente com amperímetro** e comparar com o `energia_ma` publicado no
   JSON.
5. **Ensaio de irrigação** com o tanque com água e a bomba na fonte de 7–9 V —
   primeiro pelo botão do app, depois pelo automático.
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
