# Lógica de operação

Como o vaso decide o que fazer. Este documento descreve o comportamento
implementado na v0.1 — cada regra aqui tem código correspondente em
[`include/`](../include/).

## Faixas de umidade do solo

Cinco faixas, não um limiar único. Limiar único faz a bomba oscilar em torno do
ponto de corte; faixa dá histerese natural.

| Faixa | ADC (calibração parcial de 21/09/2026) | O que acontece |
| --- | --- | --- |
| Extremamente baixa | ≥ 1650 | **Irriga.** Vira risco `SOLO MUITO SECO` |
| Baixa | 1538 – 1649 | nada |
| Estável | 1312 – 1537 | nada |
| Alta | 1200 – 1311 | nada |
| Extremamente alta | < 1200 | **Bomba bloqueada.** Risco `SOLO ENCHARCADO` |

Só o limite de cima (1650) é medido; o de baixo (1200) ainda é chute, e os dois do
meio derivam dele. Detalhes em [docs/02](02-hardware-e-pinagem.md#calibração--leia-antes-de-confiar-em-qualquer-leitura).

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

## Interface web — o app

| Rota | O que faz |
| --- | --- |
| `GET /` | a página, servida da PROGMEM, sem CDN — em campo não há internet |
| `GET /sensores` | JSON com o estado inteiro |
| `POST /foto` | pede uma foto à câmera; volta na hora |
| `GET /foto/estado` | progresso da foto enquanto ela chega pelo fio |
| `GET /foto.jpg` | a última foto pronta |
| `POST /bomba?acao=` | `ligar`, `manter` ou `desligar` |

**O celular só conversa com o vaso.** A foto vem pela UART e é servida daqui;
um endereço, uma placa na rede.

**Foto em três passos.** Uma foto leva de 2 a 3 s no fio. Uma requisição que
esperasse por ela seguraria o loop do vaso — solo, bomba, intertravamentos — esse
tempo todo. Então o pedido volta na hora, a página acompanha o progresso numa
barra, e o JPEG só é servido depois de inteiro na memória.

**Ações são `POST`.** Um `GET` que liga bomba seria acionado por qualquer coisa
que pré-carregue links: navegador, prévia de mensagem, robô.

A página busca `/sensores` a cada 2 s. Se o vaso não responder, ela diz "sem
resposta do vaso" em vez de mostrar dado velho como se fosse atual.

## A bomba pelo app

Um botão liga e desliga a bomba, **sem influenciar a lógica automática** — foi o
pedido, e ele é garantido por construção, não por cuidado:

- enquanto o app está no comando, o automático não roda — senão ele desligaria a
  bomba no fim do pulso de 4 s dele;
- o acionamento pelo app tem contadores próprios, e o automático nunca os lê.
  Pulsos, tempo total, teto do ciclo e descanso continuam exatamente onde
  estariam se o botão não existisse;
- se o automático estava no meio de um pulso quando o botão foi apertado, o pulso
  termina pelo caminho normal dele, com a contabilidade que ele mesmo faria.

**O botão não é uma chave.** É um pedido com prazo, que a página renova a cada
2 s enquanto está aberta. Se a renovação parar — aba fechada, celular bloqueado,
roteador do celular caiu —, a bomba desliga sozinha em **6 s**. E há um teto de
**30 s** por acionamento, renovando ou não. Em campo, com o roteador do celular
como única rede, uma chave que ficasse ligada esperando um "desligar" que nunca
chega esvaziaria o tanque.

**O que o botão não passa por cima.** Os intertravamentos se dividem pelo critério
de quem consegue ver o problema a tempo:

| Grupo | Intertravamentos | Vale para |
| --- | --- | --- |
| hardware | energia, tanque vazio, tanque sem leitura | automático **e** app |
| planta | solo encharcado, solo sem leitura, teto do ciclo | só automático |

Bomba girando a seco queima em minutos, e ninguém olhando o vaso vê o fundo do
tanque a tempo — então essa proteção vale sempre. Já se a planta precisa de água,
no manual quem decide é a pessoa que está olhando para ela.

Quando o botão é recusado, a página diz por quê: `nao liguei: tanque vazio`.

**Consequência que vale saber:** como o automático não enxerga o manual, ele pode
disparar um pulso logo depois de uma rega pelo app, se o sensor ainda não tiver
sentido a água — ela leva dezenas de segundos para percolar até ele. Se isso
incomodar em campo, uma linha em `bomba.h` faz o fim do manual valer como fim de
pulso, e o automático passa a respeitar o descanso. Não está feito porque o
pedido foi, explicitamente, não mexer na lógica.

## Rede

Máquina de estado, nunca laço de espera. **Em campo aberto a única
infraestrutura é o celular**, então o vaso fala em duas redes ao mesmo tempo:

| Rede | Quando existe | Endereço do vaso |
| --- | --- | --- |
| própria, `farmio-01` | **sempre** | `http://192.168.4.1`, fixo |
| roteador do celular | quando ligado e ao alcance | dado pelo celular; sai na serial e em `farmio-01.local` |

A rede própria não é "reserva para quando o roteador falha": em campo o roteador
do celular fica desligado quase o tempo todo. Uma reserva que precisasse detectar
a falha para subir estaria subindo o tempo inteiro.

As duas convivem com um custo: para procurar o roteador, o rádio sai do canal da
rede própria por ~2 s. Então, **enquanto houver alguém conectado na rede própria,
o vaso não procura o roteador** — quem está usando o vaso tem prioridade sobre
quem talvez apareça. Sem roteador, a procura usa espera crescente de 2 s até 60 s.

Sem `secrets.h`, só a rede própria sobe — e o firmware compila igual, que é o que
permite o CI rodar sem nenhuma senha.

## Serial

Um painel legível sai a cada 3 s: cada componente com o número cru, o que ele
virou e de onde veio. O monitor é bidirecional — uma letra muda o que aparece,
sem regravar: `b` painel, `l` linha, `j` JSON, `p` pinagem, `h` ajuda.

Para ensaio de bancada, os mesmos caminhos do app: `f` pede uma foto, `+` liga a
bomba como o botão liga (e ela desliga sozinha em 6 s, porque ninguém renova da
serial), `-` desliga. Com os mesmos intertravamentos — `+` com o tanque vazio
responde `bomba recusada: tanque vazio`.
