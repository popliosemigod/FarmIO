# Enlace ESP32-C3 ↔ ESP32-CAM

Como as duas placas do vaso conversam, e por que dessa forma e não de outra.

## O problema

O FarmIO tem duas placas. O **ESP32-C3** é o vaso: lê sensores, decide sobre a
bomba, desenha a tela e hospeda a página. A **ESP32-CAM** é o olho: captura,
classifica e transmite vídeo. Elas precisam de um canal por onde o vaso pergunte
"tem planta na frente?" e a câmera responda.

As duas têm rádio. A tentação óbvia é fazer a pergunta por HTTP. Não é o que o
projeto faz, por três razões concretas:

1. **Watchdog não pode depender de terceiro.** O vaso precisa saber se a câmera
   está viva mesmo com o roteador fora do ar. Enlace que atravessa infraestrutura
   de outra pessoa não serve para vigiar o parceiro.
2. **Corrente.** Dois rádios transmitindo no mesmo instante, a 10 cm um do outro,
   somam pico justamente onde o orçamento de USB é apertado — ver
   [energia](06-energia-usb.md). Fio não gasta corrente de rádio.
3. **Latência previsível.** HTTP em rede doméstica varia de 5 ms a 2 s. O fio dá
   sempre o mesmo número, e número estável é o que permite fechar um *timeout*
   honesto em vez de um chute generoso.

O vídeo continua indo por Wi-Fi. Isso é streaming, e streaming é exatamente o que
115200 bps não aguenta. Pelo fio passa só o veredito: doze bytes.

## Ligação física

| ESP32-C3 | | ESP32-CAM | Observação |
| --- | --- | --- | --- |
| GPIO20 (RX) | ← | GPIO14 (TX) | |
| GPIO21 (TX) | → | GPIO15 (RX) | GPIO15 é strapping; RX em repouso é alto, o que é o nível correto |
| GPIO7 | → | RST | dreno aberto, pull-up de 10 kΩ |
| GND | — | GND | **obrigatório**, e é o erro nº 1 de quem monta |
| 5V | — | 5V | as duas placas na mesma fonte |

Três escolhas nessa tabela merecem justificativa.

**Por que GPIO20/21 no C3.** São a UART0 padrão, que normalmente é o console. No
C3 o console vai pelo **USB nativo** (GPIO18/19), liberado por
`ARDUINO_USB_CDC_ON_BOOT=1` no `platformio.ini`. Sem isso, o log do vaso sairia
pelos mesmos fios do enlace e entraria como quadro do outro lado.

**Por que GPIO14/15 na câmera, e não o header de gravação (GPIO1/3).** Porque é
pelo GPIO1 que a ROM da ESP32-CAM cospe o log de boot a 115200 toda vez que ela
reinicia, e porque desligar o enlace para regravar a câmera viraria rotina.
GPIO14 e GPIO15 são linhas de cartão SD — e não há cartão SD neste projeto.

**Por que dreno aberto no reset.** O pino do C3 fica em alta impedância em repouso
e só vira saída em nível baixo durante o pulso. Se o C3 estiver desligado ou em
reset, a câmera não fica presa em reset por causa dele.

## Formato do quadro

```
A5 5A | VER | TIPO | LEN | ...LEN bytes de carga... | CRC16 (little endian)
 0  1 |  2  |  3   |  4  |  5 .. 5+LEN-1            | dois últimos
```

O CRC (CCITT-FALSE, polinômio 0x1021, init 0xFFFF) cobre de `VER` até o fim da
carga. Não cobre o preâmbulo, que serve só para achar o início.

**Por que preâmbulo e CRC, e não uma linha de texto terminada em `\n`.** Porque a
ESP32-CAM despeja o log do bootloader da ROM nesse mesmo par de fios toda vez que
reinicia. Sem sincronismo e verificação, esse lixo entraria como leitura válida e
viraria decisão de irrigar. Com eles, o receptor descarta byte a byte até
reencontrar o preâmbulo, e o quadro corrompido morre no CRC.

| Tipo | Sentido | Carga |
| --- | --- | --- |
| `0x01` PING | vaso → cam | — |
| `0x02` PONG | cam → vaso | versão, uptime, resolução, PSRAM (12 B) |
| `0x10` PEDE_VEREDITO | vaso → cam | — |
| `0x11` VEREDITO | cam → vaso | probabilidade, cobertura, ExG, região, clusters, ms, classe, flags (12 B) |
| `0x20` ANUNCIA_IP | cam → vaso | IP em ASCII |
| `0x30` CONFIG | vaso → cam | piso de ExG, % de bloco, dois limiares (6 B) |
| `0x7F` LOG | cam → vaso | texto livre |

**Mestre único.** Só o vaso pergunta. Isso elimina colisão sem precisar de
arbitragem nenhuma, e é o que permite ao vaso saber que uma resposta que não veio
é uma falha, e não um silêncio normal.

**`ANUNCIA_IP` resolve um incômodo real da v0.1:** era preciso digitar
`/cam?ip=192.168.0.55` na mão toda vez que o DHCP trocasse o endereço da câmera.
Agora a câmera anuncia o próprio IP a cada ping e a página acha o vídeo sozinha.

**`CONFIG` garante uma fonte só da verdade.** Os limiares vivem no `config.h` do
vaso e são empurrados para a câmera quando o enlace sobe. Sem isso existiriam dois
conjuntos de limiares no projeto, e um deles estaria sempre desatualizado.

## Escada de recuperação

Perguntar a cada 10 s, aceitar resposta em até 2,5 s:

| Falhas seguidas | O que acontece |
| --- | --- |
| 1 | nada — quadro perdido é normal em fio de jumper |
| 3 | risco `CÂMERA SEM RESPOSTA` na tela e no JSON; o vaso **continua irrigando** |
| 6 | pulso de 2 ms na linha de reset; espera 4 s pelo boot e recomeça |

Enquanto o enlace está caído, o vaso pergunta `PING` em vez de `PEDE_VEREDITO` —
é mais barato e não acorda o sensor da câmera à toa.

**A câmera nunca manda na bomba** por padrão. A razão está em `BOMBA_EXIGE_PLANTA`
no [`config.h`](../include/config.h): câmera suja ou às escuras vira "não há
planta", e o intertravamento deixaria a planta secar por causa de uma lente
empoeirada.

## O que já foi medido

Ensaio de 09/09/2026, ambiente `autoteste` rodando no ESP32-C3 da bancada
(a implementação do protocolo é literalmente a mesma dos dois lados — é a mesma
biblioteca `lib/farmio_enlace/`):

| Ensaio | Resultado |
| --- | --- |
| Veredito ida e volta, byte a byte | ✅ idêntico |
| Achar o quadro depois de 300 bytes de lixo | ✅ |
| Recuperar de preâmbulo falso com `LEN` mentiroso | ✅ |
| Dois quadros colados num só buffer | ✅ os dois saem |
| **Varredura de um bit trocado em cada posição** | **152 de 152 recusados (100%)** |

A varredura de bit é o número que importa: ela troca um bit de cada vez em cada
posição do quadro de 19 bytes e confere que nenhum deles passa. Nenhum quadro
corrompido vira veredito.

**O que ainda não foi medido:** nada disso passou por fio de verdade. Taxa de erro
real, comprimento máximo de jumper e comportamento com a câmera reiniciando de
fato são ensaio de bancada, e entram no [diário](../diario.md) quando acontecerem.

## Gravar as duas placas

```powershell
pio run -e c3  -t upload            # o vaso, pela USB nativa
pio run -e cam -t upload            # a câmera, pelo adaptador USB-TTL
```

A câmera precisa de GPIO0 no GND para entrar em gravação, como sempre. O enlace
usa GPIO14/15 e **não precisa ser desligado** para isso.
