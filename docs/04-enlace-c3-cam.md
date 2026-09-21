# Enlace ESP32-C3 ↔ ESP32-CAM

Como as duas placas do vaso conversam, e por que dessa forma e não de outra.

## O problema

O FarmIO tem duas placas. O **ESP32-C3** é o vaso: lê sensores, decide sobre a
bomba, desenha a tela e hospeda a página. A **ESP32-CAM** é o olho: captura,
classifica e, quando o app pede, tira uma foto. Elas precisam de um canal por onde o vaso pergunte
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

## Desde a versão 2, a foto também passa pelo fio

Até a v0.2 a câmera servia vídeo MJPEG por Wi-Fi, direto para o navegador. Em
21/09/2026 isso saiu, porque o projeto vai para campo aberto e lá **a única rede é
o roteador do celular**:

- cada placa a mais na rede é mais uma que precisa achar o roteador, pegar IP e
  sobreviver às quedas dele — e o celular teria de alcançar as duas;
- vídeo contínuo mantém sensor e rádio acesos o tempo todo, num projeto que vive
  numa porta USB;
- ninguém assiste a um vaso. O que se quer é **uma foto na hora de conferir** —
  e uma foto cabe no fio.

Então a câmera perdeu o rádio. O celular conversa só com o vaso; o vaso pede a
foto pela UART e a serve pela própria página. A câmera também deixou de precisar
de credencial nenhuma.

O preço é tempo: 115200 bps entregam ~11 kB/s, e uma VGA em JPEG na qualidade 14
tem de 20 a 30 kB. **De 2 a 3 s por foto** — aceitável para um botão, inviável
para vídeo, e vídeo deixou de existir.

## Ligação física

Desde 21/09/2026 a câmera é a **Seeed XIAO ESP32-S3 Sense**, no lugar da
ESP32-CAM AI-Thinker. O C3 continua sendo o vaso, e o protocolo não mudou uma
vírgula — só os pinos do lado da câmera.

| ESP32-C3 | | XIAO ESP32-S3 Sense | Observação |
| --- | --- | --- | --- |
| GPIO20 (RX) | ← | **D0** (GPIO1, TX) | |
| GPIO21 (TX) | → | **D1** (GPIO2, RX) | |
| GND | — | GND | **obrigatório**, e é o erro nº 1 de quem monta |
| 5V | — | 5V | as duas placas na mesma fonte |
| GPIO7 | | *não ligado* | a XIAO não tem pino de reset na borda — ver abaixo |

**Por que D0/D1 na XIAO, por eliminação.** D6/D7 (GPIO43/44) são a UART0, e a ROM
do S3 cospe o log de boot no GPIO43 a cada reinício, mesmo com o console no USB.
D2 (GPIO3) é strapping. D8–D10 (GPIO7–9) são o SPI do cartão SD na placa Sense.
Sobram D0 e D1, que não têm função nenhuma na Sense.

**Sem pino de reset.** O EN da XIAO só existe no botão de reset — não há pino na
borda para o C3 pulsar. O degrau de reset da escada de recuperação continua no
código do vaso, sem efeito até alguém soldar um fio no botão. Quem segura a câmera
travada agora é o **watchdog do próprio loop dela**: se o loop ficar 5 s sem voltar,
o chip reinicia sozinho.

A tabela da AI-Thinker, ambiente `cam-aithinker`, que continua compilando:

| ESP32-C3 | | ESP32-CAM AI-Thinker | Observação |
| --- | --- | --- | --- |
| GPIO20 (RX) | ← | GPIO14 (TX) | |
| GPIO21 (TX) | → | GPIO15 (RX) | GPIO15 é strapping; RX em repouso é alto, o que é o nível correto |
| GPIO7 | → | RST | dreno aberto, pull-up de 10 kΩ |
| GND | — | GND | obrigatório |
| 5V | — | 5V | |

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
| `0x20` | — | *reservado: era ANUNCIA_IP na v1; não reaproveitar* |
| `0x30` CONFIG | vaso → cam | piso de ExG, % de bloco, dois limiares (6 B) |
| `0x40` PEDE_FOTO | vaso → cam | — |
| `0x41` FOTO_INICIO | cam → vaso | tamanho u32, largura u16, altura u16, ms de captura u16 (10 B) |
| `0x42` FOTO_PEDACO | cam → vaso | deslocamento u32 + até 196 bytes do JPEG |
| `0x43` FOTO_FIM | cam → vaso | tamanho u32 + CRC16 do JPEG inteiro (6 B) |
| `0x44` FOTO_ERRO | cam → vaso | código: 1 sem sensor, 2 captura falhou, 3 formato |
| `0x7F` LOG | cam → vaso | texto livre |

**Versão 2, carga máxima de 200 bytes.** Com os 64 da v1, cada quadro de foto
levaria 60 bytes de imagem e 11 de moldura — 18% de desperdício. Com 200, são 196
e 11, menos de 6%. Placas com versões diferentes se recusam mutuamente em vez de
se entenderem pela metade: vereditos passando e fotos falhando seria o pior
diagnóstico possível.

**Deslocamento em cada pedaço.** Cada quadro já tem CRC próprio, então um pedaço
corrompido morre sozinho no receptor. Sem o deslocamento, o vaso juntaria os que
sobraram numa imagem mais curta, sem aviso. Com ele, o buraco aparece na hora e a
foto falha com o motivo escrito — melhor que uma imagem quebrada silenciosamente.

**CRC da imagem inteira no fim.** Cobre o que o CRC por quadro não cobre: a
chance de 1 em 65 mil de um quadro corrompido passar no CRC16 dele. Numa foto de
150 quadros, essa chance deixa de ser desprezível; no JPEG inteiro, volta a ser.

**Quadro velho descartado.** Com dois buffers, o driver da câmera entrega o último
quadro *completo*, que pode ter sido capturado antes de alguém mexer na cena. A
câmera descarta um antes de capturar — custa ~70 ms e garante que a foto é do
instante do pedido.

**Fila de 4 kB no vaso.** Durante a foto os bytes chegam a ~11 kB/s, e a fila de
recepção padrão (256 B) enche em 22 ms — menos que um redesenho do OLED. A fila
de 4 kB dá ~350 ms para o loop do vaso se atrasar sem perder nada.

**Mestre único.** Só o vaso pergunta. Isso elimina colisão sem precisar de
arbitragem nenhuma, e é o que permite ao vaso saber que uma resposta que não veio
é uma falha, e não um silêncio normal.

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

**Durante uma foto, a escada para.** A câmera está ocupada transmitindo, e contar
isso como falha acabaria reiniciando a câmera no meio da própria foto. A foto tem
prazo próprio, `FOTO_TIMEOUT_MS` (15 s).

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
pio run -e c3  -t upload            # o vaso, pelo USB nativo do C3
pio run -e cam -t upload            # a câmera XIAO, pelo USB-C dela
```

As duas têm USB nativo e aparecem como `Dispositivo Serial USB`, VID 303A. Para
saber qual é qual: o C3 tem MAC terminado em `0E:BC`, a XIAO em `DF:61:58` — o
`esptool flash_id` mostra.

O enlace **não precisa ser desligado** para gravar nenhuma das duas.

## Conferir a câmera sozinha, antes do enlace

Com a XIAO no USB, o console dela aceita três letras:

| Letra | O que faz |
| --- | --- |
| `s` | estado: sensor (OV2640 ou OV3660), PSRAM, resolução, contadores |
| `v` | classifica o que a câmera vê e imprime o veredito |
| `F` | manda o JPEG para o PC, entre os marcadores `@@FOTO@@` e `@@FIM@@` |

É assim que se confere a câmera antes de confiar no fio — uma coisa de cada vez.
Foi por esse caminho que saiu o primeiro teste real do classificador; ver
[docs/05](05-visao-planta.md#o-primeiro-teste-real-um-falso-positivo).
