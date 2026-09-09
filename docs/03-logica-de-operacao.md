# Lógica de operação

Como o vaso decide o que fazer. Este documento descreve o comportamento
implementado na v0.1 — cada regra aqui tem código correspondente em
[`include/`](../include/).

## Faixas de umidade do solo

Cinco faixas, não um limiar único. Limiar único faz a bomba oscilar em torno do
ponto de corte; faixa dá histerese natural.

| Faixa | ADC (padrão, a calibrar) | O que acontece |
| --- | --- | --- |
| Extremamente baixa | ≥ 2800 | **Irriga.** Vira risco `SOLO MUITO SECO` |
| Baixa | 2400 – 2799 | nada |
| Estável | 1600 – 2399 | nada |
| Alta | 1200 – 1599 | nada |
| Extremamente alta | < 1200 | **Bomba bloqueada.** Risco `SOLO ENCHARCADO` |

Leitura alta = solo seco: o sensor capacitivo entrega mais tensão quando há menos
água em volta.

## A bomba: irrigação pulsada

```
solo extremamente seco?
        │ não → não irriga
        ▼ sim
tanque vazio? ─────────── sim → BLOQUEIA (risco: tanque vazio)
solo encharcado? ──────── sim → BLOQUEIA (risco: solo encharcado)
sem leitura de solo? ──── sim → BLOQUEIA
passou do teto de 120 s?  sim → BLOQUEIA
        │ nenhum bloqueio
        ▼
liga 4 s ──► desliga ──► espera 20 s ──► mede de novo ──┐
        ▲                                                │
        └────────────────────────────────────────────────┘
```

**Por que pulsar em vez de irrigar até a leitura mudar.** A água leva dezenas de
segundos para percolar do dreno até o sensor. Em malha contínua, a bomba
despejaria o tanque inteiro antes de a leitura reagir — afogando exatamente a
planta que deveria salvar. Pulso curto, espera longa, mede de novo.

**Por que existe um teto absoluto de 120 s.** Sensor de solo com mau contato lê
"seco" para sempre. Sem teto, a bomba ficaria ligada até acabar a água ou até
queimar. O teto transforma uma falha de sensor em bomba parada, que é o modo de
falha seguro.

Intertravamento tem prioridade absoluta: se o tanque esvaziar no meio de um
pulso, a bomba desliga naquele instante, sem esperar o pulso terminar.

## Situações de risco

Riscos são **bits**, não estados exclusivos — vários podem valer ao mesmo tempo,
e calor com tanque vazio é justamente o caso ruim.

| Bit | Risco | Gatilho | Ação |
| --- | --- | --- | --- |
| 1 | Temperatura alta | > 30 °C | alerta |
| 2 | Solo muito seco | faixa extremamente baixa | irriga |
| 4 | Tanque vazio | 0% | bloqueia a bomba |
| 8 | Solo encharcado | faixa extremamente alta | bloqueia a bomba |
| 16 | Sensor sem resposta | 4 falhas seguidas do DHT22 | alerta |

Os dois últimos não estavam na especificação original como alertas visíveis. Foram
promovidos porque **bloqueio silencioso é o pior tipo**: para quem olha o vaso,
parece defeito.

## Display

- **TELA INICIAL** — temperatura em destaque, umidade do ar, faixa do solo,
  porcentagem do tanque e uma barra. Gota no canto enquanto irriga.
- **TELA DE RISCO** — aparece por 4 s, some, e reaparece a cada 30 s enquanto o
  risco durar. Além do nome do risco, diz **o que fazer**: "reabasteça o tanque",
  "irrigação bloqueada", "cheque o DHT22".

Alerta permanente vira paisagem e para de ser lido — isso estava na especificação
original e continua valendo.

## Anel de LED

| Estado | Cor | Animação |
| --- | --- | --- |
| Ligando (3 s) | verde | cometa girando, 2 s por volta |
| Funcionamento padrão | branco | entrada suave de 1,5 s |
| Situação de risco | vermelho | respiração lenta, 4 s por ciclo |
| Risco resolvido | vermelho → branco | varre o anel em 2,5 s, depois fica branco |

Nenhum pisca-pisca em lugar nenhum: o anel muda de estado deslizando. O pedido
explícito era que o LED não gerasse desconforto visual.

## Interface web

- `GET /` — página única, servida da PROGMEM, sem CDN. Mostra os mesmos dados do
  display e embute o vídeo da ESP32-CAM.
- `GET /sensores` — JSON com a leitura atual. É a rota que qualquer cliente
  consome: painel, script de bancada, outro vaso.
- `GET /cam?ip=…` — grava o IP da ESP32-CAM sem recompilar.

A página busca `/sensores` a cada 2 s. Se o nó não responder, ela diz "sem
resposta do nó" em vez de mostrar dado velho como se fosse atual.

## Rede

Máquina de estado, nunca laço de espera. Sem `secrets.h`, o vaso sobe o próprio
ponto de acesso e espera configuração — é o que permite o CI compilar sem
nenhuma senha. Com credencial e sem rede, a reconexão usa espera crescente de 2 s
até 60 s: rede fora do ar não merece uma tentativa por segundo consumindo
corrente à toa.

## Serial

O mesmo JSON de `/sensores` sai pela serial a cada 3 s. É o canal que funciona
sem rede nenhuma — e é dele que sai o número para calibrar os limiares.
