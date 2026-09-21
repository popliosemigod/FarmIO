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
  TIPO_CONFIG      = 0x30,  // vaso -> cam  ajusta cadencia e limiar
  TIPO_PEDE_FOTO   = 0x40,  // vaso -> cam  "tira uma foto e me manda"
  TIPO_FOTO_INICIO = 0x41,  // cam  -> vaso tamanho, largura, altura
  TIPO_FOTO_PEDACO = 0x42,  // cam  -> vaso deslocamento + bytes do JPEG
  TIPO_FOTO_FIM    = 0x43,  // cam  -> vaso tamanho total + CRC da imagem
  TIPO_FOTO_ERRO   = 0x44,  // cam  -> vaso codigo + texto
  TIPO_LOG         = 0x7F   // cam  -> vaso texto livre de diagnostico
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
  FOTO_ERRO_FORMATO    = 3   // quadro que nao e JPEG
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
//      while (serial.available()) rec.empurra(serial.read());
//      Quadro q;
//      while (rec.proximo(q)) trata(q);
//
//  Separar 'empurra' de 'proximo' e de proposito: um unico push pode
//  fechar dois quadros quando a serial acumulou, e API que devolve um
//  quadro por byte perderia o segundo.
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
