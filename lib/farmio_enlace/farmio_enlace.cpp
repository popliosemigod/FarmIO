// =====================================================================
//  FarmIO - farmio_enlace.cpp
//  Implementacao do protocolo do enlace C3 <-> ESP32-CAM.
//  C++11 puro, sem Arduino: o autoteste exercita este arquivo inteiro.
// =====================================================================
#include "farmio_enlace.h"

#include <string.h>

namespace Enlace {

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, sem reflexao).
//
// Por que este e nao um checksum de soma: soma de bytes nao enxerga
// troca de ordem nem erro duplo, e os dois acontecem em fio solto de
// jumper. O CCITT-FALSE pega qualquer rajada de ate 16 bits, que e a
// falha tipica de contato ruim. Bit a bit, sem tabela: 256 bytes de
// tabela custariam mais que os ~10 us por quadro que se ganha.
uint16_t crc16(const uint8_t* dados, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)dados[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

size_t monta(uint8_t tipo, const uint8_t* carga, uint8_t n, uint8_t* saida, size_t maxSaida) {
  if (n > CARGA_MAX) return 0;
  const size_t total = (size_t)CABECALHO + n + 2;
  if (maxSaida < total) return 0;

  saida[0] = PREAMBULO_A;
  saida[1] = PREAMBULO_B;
  saida[2] = VERSAO;
  saida[3] = tipo;
  saida[4] = n;
  if (n && carga) memcpy(saida + CABECALHO, carga, n);

  // O CRC comeca em VER: o preambulo e so marca de inicio e nao carrega
  // informacao que valha proteger.
  const uint16_t crc = crc16(saida + 2, (size_t)(3 + n));
  poe16(saida + CABECALHO + n, crc);
  return total;
}

Receptor::Receptor() : n_(0) {
  memset(&c_, 0, sizeof(c_));
  memset(buf_, 0, sizeof(buf_));
}

void Receptor::reinicia() {
  n_ = 0;
}

void Receptor::desliza(size_t quantos) {
  if (quantos >= n_) {
    n_ = 0;
    return;
  }
  memmove(buf_, buf_ + quantos, n_ - quantos);
  n_ -= quantos;
}

void Receptor::empurra(uint8_t b) {
  // Buffer cheio sem quadro fechado significa que o que esta dentro nao
  // era quadro. Joga o byte mais velho fora e segue: assim o receptor
  // nunca trava esperando um LEN que mentiu.
  if (n_ >= sizeof(buf_)) {
    c_.bytesDescartados++;
    desliza(1);
  }
  buf_[n_++] = b;
}

bool Receptor::proximo(Quadro& fora) {
  for (;;) {
    // 1. Alinhar o inicio do buffer no preambulo, descartando lixo.
    while (n_ >= 1 && buf_[0] != PREAMBULO_A) {
      c_.bytesDescartados++;
      desliza(1);
    }
    if (n_ >= 2 && buf_[1] != PREAMBULO_B) {
      // Achou A5 sem 5A atras: aquele A5 era lixo, nao inicio.
      c_.bytesDescartados++;
      desliza(1);
      continue;
    }
    if (n_ < CABECALHO) return false;

    const uint8_t ver  = buf_[2];
    const uint8_t tipo = buf_[3];
    const uint8_t len  = buf_[4];

    if (ver != VERSAO) {
      c_.versaoErrada++;
      c_.bytesDescartados += 2;
      desliza(2);  // pula o preambulo e volta a cacar
      continue;
    }
    if (len > CARGA_MAX) {
      c_.tamanhoErrado++;
      c_.bytesDescartados += 2;
      desliza(2);
      continue;
    }

    const size_t total = (size_t)CABECALHO + len + 2;
    if (n_ < total) return false;  // quadro ainda chegando

    const uint16_t esperado = crc16(buf_ + 2, (size_t)(3 + len));
    const uint16_t veio     = pega16(buf_ + CABECALHO + len);
    if (esperado != veio) {
      // Nao descarta o quadro inteiro: o preambulo verdadeiro pode estar
      // dentro do que se pensou ser carga. Anda dois bytes e recomeca a
      // busca - e o que faz o receptor se recuperar do log de boot da
      // camera no meio de um quadro.
      c_.crcErrado++;
      c_.bytesDescartados += 2;
      desliza(2);
      continue;
    }

    fora.tipo = tipo;
    fora.n    = len;
    if (len) memcpy(fora.carga, buf_ + CABECALHO, len);
    desliza(total);
    c_.quadrosOk++;
    return true;
  }
}

// ---------------------------------------------------------------------
void serializa(const CargaVeredito& v, uint8_t* p12) {
  poe16(p12 + 0, v.probabilidade);
  poe16(p12 + 2, v.cobertura);
  poe16(p12 + 4, (uint16_t)v.exgMedio);
  p12[6] = v.maiorRegiaoPct;
  p12[7] = v.clusters;
  poe16(p12 + 8, v.ms);
  p12[10] = v.classe;
  p12[11] = v.flags;
}

bool desserializa(const uint8_t* carga, uint8_t n, CargaVeredito& fora) {
  if (n < VEREDITO_BYTES) return false;
  fora.probabilidade  = pega16(carga + 0);
  fora.cobertura      = pega16(carga + 2);
  fora.exgMedio       = (int16_t)pega16(carga + 4);
  fora.maiorRegiaoPct = carga[6];
  fora.clusters       = carga[7];
  fora.ms             = pega16(carga + 8);
  fora.classe         = carga[10];
  fora.flags          = carga[11];
  return true;
}

void serializa(const CargaPong& p, uint8_t* p12) {
  p12[0] = p.major;
  p12[1] = p.minor;
  poe32(p12 + 2, p.uptimeS);
  poe16(p12 + 6, p.largura);
  poe16(p12 + 8, p.altura);
  p12[10] = p.temPsram;
  p12[11] = p.fps;
}

bool desserializa(const uint8_t* carga, uint8_t n, CargaPong& fora) {
  if (n < PONG_BYTES) return false;
  fora.major    = carga[0];
  fora.minor    = carga[1];
  fora.uptimeS  = pega32(carga + 2);
  fora.largura  = pega16(carga + 6);
  fora.altura   = pega16(carga + 8);
  fora.temPsram = carga[10];
  fora.fps      = carga[11];
  return true;
}

// ---------------------------------------------------------------------
//  RemontaFoto
// ---------------------------------------------------------------------
RemontaFoto::RemontaFoto()
    : buf_(0),
      total_(0),
      recebidos_(0),
      vivoEm_(0),
      pedidoEm_(0),
      prox_(0),
      reenvios_(0),
      pedido_(false),
      encerrada_(true),
      motivo_("") {
  memset(mapa_, 0, sizeof(mapa_));
}

uint16_t RemontaFoto::slots() const {
  return (uint16_t)((total_ + FOTO_PEDACO_MAX - 1) / FOTO_PEDACO_MAX);
}

uint32_t RemontaFoto::contiguo() const {
  const uint32_t d = (uint32_t)prox_ * FOTO_PEDACO_MAX;
  return d < total_ ? d : total_;
}

void RemontaFoto::inicia(uint8_t* buf, uint32_t total, uint32_t agoraMs) {
  buf_       = buf;
  total_     = total;
  recebidos_ = 0;
  prox_      = 0;
  vivoEm_    = agoraMs;
  pedidoEm_  = agoraMs;
  reenvios_  = 0;
  pedido_    = false;
  motivo_    = "";
  memset(mapa_, 0, sizeof(mapa_));
  encerrada_ = (buf == 0 || total == 0);
  if (!encerrada_ && slots() > SLOTS_MAX) {
    encerrada_ = true;
    motivo_    = "foto maior que o mapa de pedacos";
  }
}

RemontaFoto::Acao RemontaFoto::falha(const char* motivo) {
  encerrada_ = true;
  motivo_    = motivo;
  return FALHOU;
}

RemontaFoto::Acao RemontaFoto::pedeReenvio(uint32_t agoraMs) {
  // Ja pediu e o prazo de resposta ainda nao venceu: o resto da rajada que
  // segue chegando nao e motivo para pedir de novo.
  if (pedido_ && (uint32_t)(agoraMs - pedidoEm_) < ESPERA_MS) return NADA;
  if (reenvios_ >= REENVIOS_MAX) return falha("pedaco perdido no fio, sem recuperar");
  reenvios_++;
  pedido_   = true;
  pedidoEm_ = agoraMs;
  vivoEm_   = agoraMs;  // o proximo "parado" so vale depois de ESPERA_MS
  return PEDE_REENVIO;
}

RemontaFoto::Acao RemontaFoto::pedaco(uint32_t desloc, const uint8_t* dados, uint32_t n,
                                      uint32_t agoraMs) {
  if (encerrada_) return NADA;
  vivoEm_ = agoraMs;  // qualquer quadro da foto prova que o fio esta vivo

  // Pedaco que nao cabe no molde - fora do alinhamento, alem do fim ou com
  // tamanho estranho - e ignorado. Um quadro com CRC certo e conteudo
  // absurdo e improvavel; se vier, o CRC do JPEG inteiro ainda decide.
  if (desloc % FOTO_PEDACO_MAX != 0 || desloc >= total_) return NADA;
  const uint16_t slot = (uint16_t)(desloc / FOTO_PEDACO_MAX);
  const uint32_t esperado =
      (total_ - desloc) < FOTO_PEDACO_MAX ? (total_ - desloc) : (uint32_t)FOTO_PEDACO_MAX;
  if (n != esperado) return NADA;
  if (tem(slot)) return NADA;  // repetido

  const bool buraco = slot > prox_;  // ha pedaco faltando antes deste
  memcpy(buf_ + desloc, dados, n);
  mapa_[slot >> 3] |= (uint8_t)(1 << (slot & 7));
  recebidos_ += n;

  const uint16_t antes = prox_;
  while (prox_ < slots() && tem(prox_)) prox_++;
  if (prox_ > antes) pedido_ = false;  // voltou ao trilho: novo buraco merece novo pedido

  return buraco ? pedeReenvio(agoraMs) : NADA;
}

RemontaFoto::Acao RemontaFoto::fim(uint32_t total, uint16_t crc, uint32_t agoraMs) {
  if (encerrada_) return NADA;
  vivoEm_ = agoraMs;

  if (total != total_) return falha("tamanho do FIM diferente do INICIO");
  if (recebidos_ < total_) return pedeReenvio(agoraMs);

  if (crc16(buf_, total_) != crc) {
    // Todos os quadros passaram no CRC deles e a imagem, mesmo assim, esta
    // errada. Nao ha como saber qual pedaco: recomeca do zero.
    recebidos_ = 0;
    prox_      = 0;
    pedido_    = false;
    memset(mapa_, 0, sizeof(mapa_));
    return pedeReenvio(agoraMs);
  }
  encerrada_ = true;
  return PRONTA;
}

RemontaFoto::Acao RemontaFoto::parado(uint32_t agoraMs) {
  if (encerrada_) return NADA;
  if ((uint32_t)(agoraMs - vivoEm_) < ESPERA_MS) return NADA;
  return pedeReenvio(agoraMs);
}

}  // namespace Enlace
