# FarmIO

**Vaso inteligente e automático.** Irriga sozinho, avisa o que está errado e
mostra tudo em três lugares: no display, na página web e na serial.

> **Estado: compila e está dimensionado. Nenhuma placa foi gravada ainda.**
> Todo número neste repositório vem de datasheet ou de cálculo. Os limiares dos
> sensores são ponto de partida, não medida — ver [calibração](docs/02-hardware-e-pinagem.md#calibração--leia-antes-de-confiar-em-qualquer-leitura).

Este é um projeto do laboratório [**Jaspy**](https://github.com/popliosemigod/Jaspy),
que guarda o método, as ferramentas e a documentação comum. Cada projeto tem
repositório próprio; este é o do FarmIO.

## O que ele faz

- lê temperatura e umidade do ar (DHT22), umidade do solo e nível do tanque;
- **irriga em pulsos** quando o solo fica extremamente seco, e para na hora se o
  tanque esvaziar ou o solo encharcar;
- classifica o solo em cinco faixas em vez de um limiar único — limiar único faz
  a bomba oscilar em torno do ponto de corte;
- alerta em três canais: tela, anel de LED e página web;
- transmite vídeo ao vivo, com a ESP32-CAM embutida na mesma página.

## Hardware

| Item | Componente |
| --- | --- |
| Controle | ESP32 DevKit V1 |
| Câmera | ESP32-CAM (placa separada, só streaming) |
| Ar | DHT22 |
| Solo | sensor capacitivo de umidade |
| Tanque | sensor de nível tipo pente (Funduino) |
| Tela | OLED SSD1306 128×64, I²C |
| Luz | anel de 16 LEDs WS2812 (5050) |
| Bomba | RS-385 12 V via TB6612FNG |

Pinagem completa, alimentação e as armadilhas do ADC2 estão em
[`docs/02-hardware-e-pinagem.md`](docs/02-hardware-e-pinagem.md).

## Compilar e gravar

```powershell
pio run                        # compila
pio run -t upload              # grava
pio device monitor -b 115200   # acompanha
pio run -e bancada -t upload   # log detalhado, para calibrar
```

Credencial de Wi-Fi é opcional para compilar:

```powershell
Copy-Item include\secrets.example.h include\secrets.h   # e preencher
```

Sem `secrets.h` o firmware compila e roda — o vaso sobe o próprio ponto de acesso
(`farmio-01`) e espera configuração. É isso que permite o CI compilar sem nenhuma
senha.

## Consumo de recursos

Compilação de 02/09/2026, ESP32 DevKit V1:

| | Uso |
| --- | --- |
| RAM | 14,1% — 46,3 kB de 320 kB |
| Flash | 64,4% — 844 kB de 1,31 MB |

## Estrutura

```
FarmIO/
├── platformio.ini      ambientes, bibliotecas fixadas, build flags
├── include/
│   ├── config.h        pinagem, limiares, estados  ← só isso muda ao trocar de placa
│   ├── sensores.h      DHT22, solo, tanque, filtro de mediana
│   ├── bomba.h         acionamento e intertravamentos
│   ├── anel.h          anel WS2812, animações não bloqueantes
│   ├── tela.h          OLED, tela inicial e tela de risco
│   ├── web.h           servidor HTTP, JSON e vídeo
│   └── secrets.example.h
├── src/main.cpp        boot, rede e loop
├── docs/               herança do SmartFarm, hardware, lógica
└── diario.md           previsto × medido, a cada iteração
```

## Documentação

- [Herança do SmartFarm](docs/01-heranca-smartfarm.md) — de onde o projeto veio,
  a especificação original preservada e o que mudou
- [Hardware e pinagem](docs/02-hardware-e-pinagem.md) — ligação, alimentação,
  ADC2 × Wi-Fi e o roteiro de calibração
- [Lógica de operação](docs/03-logica-de-operacao.md) — faixas, intertravamentos,
  riscos, telas e anel
- [Diário](diario.md) — cada iteração com previsto ao lado de medido

## Próximos passos

1. **Gravar numa placa.** É o que transforma "compila" em "funciona" e destrava
   todo o resto.
2. **Calibrar solo e tanque** com o ambiente `bancada`, e commitar os números
   medidos com tipo `calib`.
3. **Ensaio de irrigação** com planta real: medir quantos pulsos são necessários
   para sair da faixa seca, e ajustar `BOMBA_PASSO_MS` e `BOMBA_DESCANSO_MS`.
4. **Diagnóstico de qualidade foliar** pela câmera — é o que separa o FarmIO do
   SmartFarm e ainda não existe.
5. **Acoplamento entre vasos**, que dá nome ao projeto. Nenhum protocolo definido
   ainda.

## Convenções

Commits seguem `tipo(subsistema): descrição`, validados pelo `commitizen`
([`cz.toml`](cz.toml)). Branches: `main` guarda o estado coerente e publicado,
`develop` integra o trabalho em curso, `feat/<assunto>` para tarefa curta.

**Tag de versão só nasce de coisa medida.** A v0.1 está na `main` porque compila
e está documentada, mas não recebeu tag — tag em firmware que nunca gravou numa
placa transforma a linha do tempo em ficção.

```powershell
python -m pip install --user pre-commit commitizen
pre-commit install --install-hooks
pre-commit install --hook-type commit-msg
```
