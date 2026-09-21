// =====================================================================
//  FarmIO - farmio_enlace.h
//  Protocolo do enlace serial entre o ESP32-C3 (o vaso) e a ESP32-CAM.
//
//  POR QUE UART E NAO WI-FI. As duas placas poderiam conversar por HTTP,
//  ja que as duas tem radio. Nao conversam, por tres razoes medidas na
//  mesa e nao na intuicao:
//
//    1. O vaso precisa saber se a camera esta viva mesmo com o roteador
//       fora do ar. Enlace que depende de infraestrutura de terceiro nao
//       serve de watchdog.
//    2. Dois radios transmitindo no mesmo instante, a 10 cm um do outro,
//       somam pico de corrente justo onde o orcamento de USB e apertado.
//       Fio nao gasta corrente de radio.
//    3. Latencia de HTTP em rede domestica varia de 5 ms a 2 s. O fio da
//       sempre o mesmo numero, e numero estavel e o que permite fechar
//       um timeout honesto.
//
//  DESDE A VERSAO 2 A FOTO TAMBEM PASSA PELO FIO. A v1 servia video por
//  Wi-Fi, direto da camera para o navegador. Em campo aberto isso deixou
//  de fazer sentido: o unico acesso e o roteador do celular, e cada placa
//  a mais na rede e uma placa a mais para achar o roteador, pegar IP e
//  cair. Com a foto no fio, a camera nao precisa de radio nenhum - o
//  celular so conversa com o vaso, e o vaso busca a foto pela UART.
//
//  O preco e tempo: 115200 bps entregam ~11 kB/s, e uma VGA em JPEG tem
//  de 20 a 30 kB. Dois a tres segundos por foto. Para um botao de "tirar
//  foto", e aceitavel; para video, nao seria - e video deixou de existir.
//
//  FORMATO DO QUADRO
//
//    A5 5A | VER | TIPO | LEN | ...LEN bytes... | CRC16 (little endian)
//     0  1 |  2  |  3   |  4  |  5 .. 5+LEN-1   | dois ultimos
//
//  O CRC cobre de VER ate o fim da carga - nao cobre o preambulo, que
//  serve so para achar o inicio.
//
//  POR QUE PREAMBULO E CRC E NAO UMA LINHA DE TEXTO. A ESP32-CAM cospe
//  o log do bootloader da ROM no mesmo par de fios toda vez que reinicia.
//  Sem sincronismo e verificacao, esse lixo entraria como leitura valida.
//  Com eles, o receptor descarta byte a byte ate reencontrar o preambulo,
//  e o quadro corrompido morre no CRC em vez de virar decisao de irrigar.
//
//  Este arquivo NAO inclui Arduino.h de proposito: e C++11 puro, para
//  poder ser exercitado pelo autoteste sem placa e por qualquer host.
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Enlace {

static const uint8_t PREAMBULO_A = 0xA5;
static const uint8_t PREAMBULO_B = 0x5A;
// VERSAO 2: quadros de foto e carga maxima de 200 bytes. Placas com
// versoes diferentes se recusam mutuamente em vez de se entenderem pela
// metade - vereditos passando e fotos falhando seria o pior diagnostico.
static const uint8_t VERSAO = 2;

// 200 e nao 64 por causa da foto: com 64, cada quadro carregaria 60 bytes
// de imagem e 11 de moldura, 18% de desperdicio. Com 200, sao 196 e 11,
// menos de 6%. LEN e um byte, entao o teto duro seria 255.
static const uint8_t CARGA_MAX  = 200;
static const uint8_t CABECALHO  = 5;  // A5 5A VER TIPO LEN
static const uint8_t QUADRO_MAX = CABECALHO + CARGA_MAX + 2;

// Tipos de quadro. O vaso e o mestre: so ele pergunta, a camera so
// responde. Mestre unico elimina colisao sem precisar de arbitragem.
enum Tipo : uint8_t {
  TIPO_PING          = 0x01,  // vaso -> cam  "voce esta viva?"
  TIPO_PONG          = 0x02,  // cam  -> vaso identificacao e uptime
  TIPO_PEDE_VEREDITO = 0x10,  // vaso -> cam  "olha e me diz"
  TIPO_VEREDITO      = 0x11,  // cam  -> vaso resultado da classificacao
  // 0x20 era TIPO_ANUNCIA_IP, da v1: a camera anunciava o IP do video.
  // Reservado - nao reaproveitar, para uma placa velha nunca confundir.
  TIPO_CONFIG       = 0x30,  // vaso -> cam  ajusta cadencia e limiar
  TIPO_PEDE_FOTO    = 0x40,  // vaso -> cam  "tira uma foto e me manda"
  TIPO_FOTO_INICIO  = 0x41,  // cam  -> vaso tamanho, largura, altura
  TIPO_FOTO_PEDACO  = 0x42,  // cam  -> vaso deslocamento + bytes do JPEG
  TIPO_FOTO_FIM     = 0x43,  // cam  -> vaso tamanho total + CRC da imagem
  TIPO_FOTO_ERRO    = 0x44,  // cam  -> vaso codigo + texto
  TIPO_FOTO_REENVIA = 0x45,  // vaso -> cam  "reenvie a foto a partir deste byte"
  TIPO_LOG          = 0x7F   // cam  -> vaso texto livre de diagnostico
};

// ---- Cargas da foto ---------------------------------------------------
//
//  INICIO (10 B) - tamanho u32 | largura u16 | altura u16 | ms captura u16
//  PEDACO (4+n)  - deslocamento u32 | ate FOTO_PEDACO_MAX bytes do JPEG
//  FIM    (6 B)  - tamanho u32 | CRC16 do JPEG inteiro
//  ERRO   (1+n)  - codigo u8 | texto
//
//  O DESLOCAMENTO EM CADA PEDACO e o que torna perda detectavel. Cada
//  quadro ja tem CRC proprio, entao um pedaco corrompido morre sozinho no
//  receptor - e sem deslocamento, o vaso juntaria os que sobraram numa
//  imagem mais curta e sem aviso. Com ele, o buraco aparece na hora.
//
//  O CRC DO FIM cobre o que o CRC por quadro nao cobre: a chance de 1 em
//  65 mil de um quadro corrompido passar no CRC16 dele. Numa foto de 150
//  quadros essa chance deixa de ser desprezivel; no JPEG inteiro, volta a
//  ser.
static const uint8_t FOTO_INICIO_BYTES = 10;
static const uint8_t FOTO_FIM_BYTES    = 6;
static const uint8_t FOTO_PEDACO_MAX   = CARGA_MAX - 4;

enum ErroFoto : uint8_t {
  FOTO_ERRO_SEM_CAMERA = 1,  // o sensor nao iniciou
  FOTO_ERRO_CAPTURA    = 2,  // esp_camera_fb_get devolveu nada
  FOTO_ERRO_FORMATO    = 3,  // quadro que nao e JPEG
  FOTO_ERRO_SEM_COPIA  = 4   // pediram reenvio de uma foto que a camera nao guarda mais
};

// ---- Retransmissao da foto --------------------------------------------
//
//  REENVIA (8 B) - vaso -> cam: deslocamento u32 | tamanho total u32
//
//  O fio entre as placas e um jumper de bancada, e perde quadro: medido em
//  21/09/2026, 2 CRC ruins e 381 B de lixo em 43 kB - cerca de 1%. Com
//  70 a 150 quadros por foto, uma foto sem retransmissao falha com
//  frequencia. Perder a foto inteira por um quadro de 196 bytes e o pior
//  custo-beneficio do protocolo.
//
//  O vaso pede "a partir do byte X"; a camera, que guarda a ultima foto,
//  reenvia dali ate o FIM. O TAMANHO TOTAL no pedido e uma trava: se a
//  camera ja tirou outra foto, o tamanho nao bate e ela recusa, em vez de
//  mandar pedacos de uma imagem para remendar outra.
static const uint8_t FOTO_REENVIA_BYTES = 8;

// Remontagem da foto no vaso, sem nada de Arduino - e por isso o autoteste
// consegue simular um fio ruim e provar que fecha. Quem chama poe o relogio.
//
//  Politica:
//   - todo pedaco que chega vai para o lugar dele e e marcado num mapa,
//     mesmo adiante de um buraco: jogar fora o que veio depois do buraco
//     obrigaria a camera a reenviar o que ja chegou, e cada reenvio
//     arrisca de novo os mesmos quadros;
//   - buraco detectado pede reenvio UMA vez, a partir do primeiro pedaco
//     que falta; a camera reenvia dali ate o FIM, e o que ja estava no
//     mapa chega repetido e e ignorado;
//   - FIM incompleto pede reenvio; FIM completo com CRC errado recomeca do
//     zero (o CRC do JPEG pega o que o CRC de cada quadro deixou passar);
//   - nada chegando por ESPERA_MS pede de novo: cobre o FIM perdido e o
//     proprio pedido de reenvio perdido;
//   - depois de REENVIOS_MAX pedidos, desiste com motivo - fio ruim demais
//     para prometer foto, e melhor dizer isso do que insistir para sempre.
//
//  O mapa trabalha em pedacos de FOTO_PEDACO_MAX bytes alinhados em zero,
//  que e como a camera fatia a foto. Pedido de reenvio sempre comeca num
//  limite de pedaco, entao o alinhamento se mantem.
class RemontaFoto {
public:
  enum Acao : uint8_t {
    NADA = 0,
    PEDE_REENVIO,
    PRONTA,
    FALHOU
  };

  static const uint8_t REENVIOS_MAX = 12;
  static const uint32_t ESPERA_MS   = 1200;
  static const uint16_t SLOTS_MAX   = 512;  // 512 x 196 B = 100 kB de foto, no maximo

  RemontaFoto();

  // 'buf' tem 'total' bytes e e do chamador.
  void inicia(uint8_t* buf, uint32_t total, uint32_t agoraMs);

  Acao pedaco(uint32_t desloc, const uint8_t* dados, uint32_t n, uint32_t agoraMs);
  Acao fim(uint32_t total, uint16_t crc, uint32_t agoraMs);
  Acao parado(uint32_t agoraMs);  // chamar de vez em quando enquanto recebe

  uint32_t recebido() const { return recebidos_; }  // bytes ja no lugar, para o progresso
  uint32_t contiguo() const;  // primeiro byte que falta: de onde pedir reenvio
  uint32_t total() const { return total_; }
  uint8_t reenvios() const { return reenvios_; }
  const char* motivo() const { return motivo_; }

private:
  Acao pedeReenvio(uint32_t agoraMs);
  Acao falha(const char* motivo);
  bool tem(uint16_t slot) const { return mapa_[slot >> 3] & (1 << (slot & 7)); }
  uint16_t slots() const;

  uint8_t* buf_;
  uint32_t total_, recebidos_;
  uint32_t vivoEm_, pedidoEm_;
  uint16_t prox_;  // primeiro pedaco que falta
  uint8_t reenvios_;
  bool pedido_, encerrada_;
  const char* motivo_;
  uint8_t mapa_[SLOTS_MAX / 8];
};

struct Quadro {
  uint8_t tipo;
  uint8_t n;
  uint8_t carga[CARGA_MAX];
};

// Diagnostico do enlace. Estes numeros vao para o JSON do vaso: enlace
// que degrada devagar (CRC subindo) avisa antes de morrer de vez.
struct Contadores {
  uint32_t quadrosOk;
  uint32_t crcErrado;
  uint32_t versaoErrada;
  uint32_t tamanhoErrado;
  uint32_t bytesDescartados;  // lixo fora de quadro - log de boot da CAM
};

uint16_t crc16(const uint8_t* dados, size_t n);

// Monta um quadro em 'saida'. Devolve o tamanho, ou 0 se nao coube.
size_t monta(uint8_t tipo, const uint8_t* carga, uint8_t n, uint8_t* saida, size_t maxSaida);

// ---------------------------------------------------------------------
//  Receptor: alimenta-se byte a byte e entrega quadros inteiros.
//
//  Uso:
//      Quadro q;
//      while (serial.available()) {
//        rec.empurra(serial.read());
//        while (rec.proximo(q)) trata(q);
//      }
//
//  O receptor guarda UM quadro (QUADRO_MAX bytes). Esvaziar a cada byte
//  e obrigatorio: empurrar a serial inteira antes de chamar proximo()
//  transborda o buffer quando chega rajada - a foto, por exemplo - e o
//  que transborda e descartado pela frente, levando quadros inteiros.
// ---------------------------------------------------------------------
class Receptor {
public:
  Receptor();

  void reinicia();
  void empurra(uint8_t b);
  bool proximo(Quadro& fora);

  const Contadores& contadores() const { return c_; }

private:
  void desliza(size_t quantos);

  uint8_t buf_[QUADRO_MAX];
  size_t n_;
  Contadores c_;
};

// ---- Ajudantes de carga: little endian explicito ---------------------
//  Nunca despejar struct no fio. As duas pontas sao little endian hoje,
//  mas alinhamento e padding de struct mudam com compilador e com flag
//  de otimizacao - e o bug resultante aparece so as vezes.
inline void poe16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

inline uint16_t pega16(const uint8_t* p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

inline void poe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint32_t pega32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---- Carga do TIPO_VEREDITO: 12 bytes --------------------------------
static const uint8_t VEREDITO_BYTES = 12;

struct CargaVeredito {
  uint16_t probabilidade;  // permil, 0..1000
  uint16_t cobertura;      // permil de pixels verdes
  int16_t exgMedio;        // indice de excesso de verde na mascara
  uint8_t maiorRegiaoPct;  // % da cena ocupada pelo maior aglomerado
  uint8_t clusters;        // quantos aglomerados separados
  uint16_t ms;             // tempo de processamento na camera
  uint8_t classe;          // 0 sem planta, 1 provavel, 2 planta
  uint8_t flags;           // bit0 luz baixa  bit1 estourado  bit2 falha
};

static const uint8_t FLAG_LUZ_BAIXA = 1 << 0;
static const uint8_t FLAG_ESTOURADO = 1 << 1;
static const uint8_t FLAG_FALHA_CAM = 1 << 2;

void serializa(const CargaVeredito& v, uint8_t* p12);
bool desserializa(const uint8_t* carga, uint8_t n, CargaVeredito& fora);

// ---- Carga do TIPO_PONG: 12 bytes ------------------------------------
static const uint8_t PONG_BYTES = 12;

struct CargaPong {
  uint8_t major;
  uint8_t minor;
  uint32_t uptimeS;
  uint16_t largura;
  uint16_t altura;
  uint8_t temPsram;
  uint8_t fps;
};

void serializa(const CargaPong& p, uint8_t* p12);
bool desserializa(const uint8_t* carga, uint8_t n, CargaPong& fora);

}  // namespace Enlace
