// =====================================================================
//  FarmIO - camera.h
//  O lado VASO do enlace com a ESP32-CAM. O vaso e o mestre: so ele
//  pergunta, e por isso nunca ha duas placas falando ao mesmo tempo.
//
//  MAQUINA DE ESTADO, NUNCA ESPERA. A regra numero um do firmware vale
//  aqui com forca dobrada: a camera pode estar desligada, sem firmware,
//  com o fio solto ou reiniciando, e em nenhum desses casos o vaso pode
//  parar de medir solo e de decidir sobre a bomba. Entao nao existe
//  "espera resposta": existe "mandei, e ate tal instante eu aceito
//  resposta".
//
//  ESCADA DE RECUPERACAO, do mais barato ao mais caro:
//
//    1 falha ....... nada. Um quadro perdido e normal em fio de jumper.
//    3 falhas ...... risco CAMERA SEM RESPOSTA: aparece na tela e no
//                    JSON. O vaso continua irrigando normalmente.
//    6 falhas ...... pulso de 2 ms na linha de reset da camera. E a
//                    unica recuperacao possivel sem ninguem na bancada,
//                    e resolve o caso real mais comum, que e a camera
//                    travada por falha de alimentacao momentanea.
//
//  O reset e por DRENO ABERTO: o pino do C3 vira entrada (alta
//  impedancia) em repouso e so vira saida em nivel baixo durante o
//  pulso. Se o C3 estiver desligado ou em reset, a camera nao fica
//  presa em reset por causa dele.
//
//  A CAMERA NUNCA MANDA NA BOMBA por padrao - ver BOMBA_EXIGE_PLANTA em
//  config.h e a razao escrita la.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"
#include "farmio_enlace.h"
#include "farmio_visao.h"
#include "energia.h"

namespace Camera {

inline HardwareSerial& porta() {
  static HardwareSerial s(1);
  return s;
}

inline Enlace::Receptor& receptor() {
  static Enlace::Receptor r;
  return r;
}

inline Visao::Filtro& filtro() {
  static Visao::Filtro f;
  return f;
}

// Instante ate o qual a resposta da pergunta em curso ainda vale. Zero
// quer dizer que nao ha pergunta no ar.
inline uint32_t& prazo() {
  static uint32_t t = 0;
  return t;
}

inline uint32_t& proximaPergunta() {
  static uint32_t t = 0;
  return t;
}

inline uint16_t& falhasSeguidas() {
  static uint16_t n = 0;
  return n;
}

inline void envia(uint8_t tipo, const uint8_t* carga, uint8_t n) {
  uint8_t q[Enlace::QUADRO_MAX];
  const size_t t = Enlace::monta(tipo, carga, n, q, sizeof(q));
  if (t) porta().write(q, t);
}

// Empurra os limiares do config.h para a camera. O vaso e o dono dos
// numeros; a camera so executa. Sem isso existiriam dois limiares no
// projeto, e um deles estaria sempre desatualizado.
inline void mandaConfig() {
  uint8_t c[6];
  c[0] = VISAO_PISO_EXG;
  c[1] = VISAO_BLOCO_VERDE_PCT;
  Enlace::poe16(c + 2, VISAO_LIMIAR_PLANTA);
  Enlace::poe16(c + 4, VISAO_LIMIAR_DUVIDA);
  envia(Enlace::TIPO_CONFIG, c, sizeof(c));
}

// Pulso de reset em dreno aberto.
inline void reiniciaCamera() {
  if (PIN_CAM_RST < 0) return;
  pinMode(PIN_CAM_RST, OUTPUT);
  digitalWrite(PIN_CAM_RST, LOW);
  delayMicroseconds(ENLACE_RESET_PULSO_US);  // unica espera do modulo, e e um pulso
  pinMode(PIN_CAM_RST, INPUT);               // volta a alta impedancia; o pull-up sobe
  V.resets++;
  falhasSeguidas() = 0;
  // Dar tempo de a camera bootar antes de considerar a proxima falha.
  proximaPergunta() = millis() + ENLACE_ESPERA_BOOT_MS;
  prazo()           = 0;
  Serial.println("[cam] sem resposta ha tempo demais - reiniciando a camera");
}

inline void begin() {
  memset(&V, 0, sizeof(V));
  V.classe = Visao::SEM_PLANTA;

  porta().begin(ENLACE_BAUD, SERIAL_8N1, PIN_CAM_RX, PIN_CAM_TX);
  if (PIN_CAM_RST >= 0) pinMode(PIN_CAM_RST, INPUT);  // dreno aberto em repouso

  filtro().reinicia();
  // Primeira pergunta logo, para a tela ja nascer com uma informacao.
  proximaPergunta() = millis() + 1500;
  Serial.printf("[cam] enlace em RX=%d TX=%d a %d bps\n", PIN_CAM_RX, PIN_CAM_TX, ENLACE_BAUD);
}

inline void trataVeredito(const Enlace::Quadro& q) {
  Enlace::CargaVeredito v;
  if (!Enlace::desserializa(q.carga, q.n, v)) return;

  V.probabilidade  = v.probabilidade;
  V.cobertura      = v.cobertura;
  V.classe         = v.classe;
  V.flags          = v.flags;
  V.msCamera       = v.ms;
  V.ultimoQuadroEm = millis();
  V.quadros++;

  // Quadro que a camera nao conseguiu tirar, ou tirou no escuro, nao
  // entra no filtro: entraria como "sem planta" e derrubaria a media por
  // um motivo que nada tem a ver com haver ou nao planta.
  const bool aproveitavel =
      !(v.flags & (Enlace::FLAG_FALHA_CAM | Enlace::FLAG_LUZ_BAIXA | Enlace::FLAG_ESTOURADO));
  if (aproveitavel) filtro().empurra(v.probabilidade);

  V.temPlanta     = filtro().temPlanta();
  V.mediaFiltrada = filtro().media();
}

inline void trata(const Enlace::Quadro& q) {
  prazo()          = 0;  // chegou resposta: nao ha mais pergunta no ar
  falhasSeguidas() = 0;

  const bool eraMuda = !V.enlaceOk;
  V.enlaceOk         = true;
  if (eraMuda) {
    Serial.println("[cam] enlace de pe");
    mandaConfig();
  }

  switch (q.tipo) {
    case Enlace::TIPO_PONG: {
      Enlace::CargaPong p;
      if (Enlace::desserializa(q.carga, q.n, p)) {
        Serial.printf("[cam] viva: fw %u.%u, %ux%u, psram %s, %lu s no ar\n", p.major, p.minor,
                      p.largura, p.altura, p.temPsram ? "sim" : "nao", (unsigned long)p.uptimeS);
      }
      break;
    }

    case Enlace::TIPO_VEREDITO: trataVeredito(q); break;

    case Enlace::TIPO_ANUNCIA_IP: {
      // A camera anuncia o proprio IP: a pagina do vaso passa a achar o
      // video sozinha, sem ninguem digitar endereco em /cam?ip=.
      const uint8_t n = q.n < sizeof(V.ip) - 1 ? q.n : (uint8_t)(sizeof(V.ip) - 1);
      memcpy(V.ip, q.carga, n);
      V.ip[n] = 0;
      break;
    }

    case Enlace::TIPO_LOG: {
      char txt[Enlace::CARGA_MAX + 1];
      const uint8_t n = q.n < Enlace::CARGA_MAX ? q.n : Enlace::CARGA_MAX;
      memcpy(txt, q.carga, n);
      txt[n] = 0;
      Serial.printf("[cam-log] %s\n", txt);
      break;
    }

    default: break;
  }
}

// Chamar todo loop. 'bombaLigada' entra por causa do orcamento de
// energia: nao se pede quadro com a bomba girando.
inline void tick(bool bombaLigada) {
  // ---- Recebe o que chegou -------------------------------------------
  while (porta().available()) receptor().empurra((uint8_t)porta().read());
  Enlace::Quadro q;
  while (receptor().proximo(q)) trata(q);

  const uint32_t agora = millis();

  // ---- Pergunta no ar que venceu o prazo ------------------------------
  if (prazo() && (int32_t)(agora - prazo()) >= 0) {
    prazo() = 0;
    V.falhas++;
    if (falhasSeguidas() < 0xFFFF) falhasSeguidas()++;

    if (falhasSeguidas() >= ENLACE_FALHAS_PARA_MUDA) V.enlaceOk = false;
    if (falhasSeguidas() >= ENLACE_FALHAS_PARA_RESET) {
      reiniciaCamera();
      return;
    }
  }

  // ---- Hora de perguntar de novo --------------------------------------
  if (prazo()) return;                                   // ja ha pergunta no ar
  if ((int32_t)(agora - proximaPergunta()) < 0) return;  // ainda nao e hora

  if (!Energia::podeCapturar(bombaLigada)) {
    // Bomba girando: adia sem contar como falha. Ver energia.h.
    proximaPergunta() = agora + 2000;
    return;
  }

  proximaPergunta() = agora + ENLACE_INTERVALO_MS;
  prazo()           = agora + ENLACE_TIMEOUT_MS;

  // Enquanto o enlace estiver caido, pergunta PING - que e barato e nao
  // acorda o sensor da camera. So pede veredito de quem ja respondeu.
  envia(V.enlaceOk ? Enlace::TIPO_PEDE_VEREDITO : Enlace::TIPO_PING, nullptr, 0);
}

// Riscos que a camera adiciona ao conjunto.
inline uint8_t riscos() {
  uint8_t r = RISCO_NENHUM;
  if (!V.enlaceOk && V.falhas > 0) r |= RISCO_CAMERA_MUDA;
  // So reclama de "sem planta" com o filtro cheio: opinar com duas
  // amostras seria alarme na hora de ligar o vaso.
  if (V.enlaceOk && filtro().amostras() >= 4 && !V.temPlanta) r |= RISCO_SEM_PLANTA;
  return r;
}

}  // namespace Camera
