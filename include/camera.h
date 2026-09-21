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
//
//  FOTO SOB DEMANDA. O app pede, o vaso repassa pelo fio, a camera
//  captura e devolve o JPEG em pedacos de 196 bytes. Enquanto a foto
//  esta chegando o vaso nao pergunta mais nada a camera - ela esta
//  ocupada transmitindo, e contar isso como falha acabaria reiniciando a
//  camera no meio da propria foto.
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

// ---------------------------------------------------------------------
//  Estado do fio: o que de fato chegou na UART, independente de o
//  vaso ter gostado. E o que separa "camera muda" de "fio errado" de
//  "camera falando e o vaso nao entendendo" - tres defeitos que, vistos
//  so pelo veredito, aparecem todos como "sem quadros".
// ---------------------------------------------------------------------
struct Fio {
  uint32_t bytes;                              // bytes que chegaram na UART, bons ou nao
  uint32_t ultimoByteEm;                       // millis() do ultimo byte
  uint32_t enviados;                           // quadros que o vaso mandou
  uint32_t pong, veredito, fotoQuadros, logs;  // quadros validos da camera
  uint32_t ecos;                               // quadros que SO o vaso envia, chegando de volta
  uint32_t maiorVoltaMs;  // maior intervalo entre duas voltas do loop principal
  uint32_t ultimaVoltaEm;
  // Erros que a propria UART reporta. Estouro de FIFO ou de buffer e byte
  // que sumiu porque o loop demorou a ler - o que o CRC so ve depois.
  volatile uint32_t errFifo, errBuffer, errQuadro;
};

inline Fio& fio() {
  static Fio f = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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

// ---------------------------------------------------------------------
//  Foto
// ---------------------------------------------------------------------
enum EstadoFoto : uint8_t {
  FOTO_NENHUMA = 0,
  FOTO_PEDIDA,     // pedido saiu, camera ainda nao respondeu
  FOTO_RECEBENDO,  // chegou o INICIO, pedacos entrando
  FOTO_PRONTA,
  FOTO_ERRO
};

struct Foto {
  uint8_t estado;
  uint8_t* buf;
  uint32_t total;
  uint32_t recebido;
  uint16_t largura, altura;
  uint16_t msCaptura;
  uint16_t crc;
  uint32_t pedidaEm;
  uint32_t duracaoMs;  // do pedido ate o ultimo byte
  uint32_t numero;     // quantas fotos ja ficaram prontas
  const char* erro;
  uint8_t reenvios;    // quantas vezes o vaso pediu pedaco de novo, nesta foto
  uint8_t tentativas;  // quantas vezes o PEDIDO da foto foi repetido
};

inline Foto& foto() {
  static Foto f = {FOTO_NENHUMA, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, ""};
  return f;
}

inline bool fotoEmCurso() {
  return foto().estado == FOTO_PEDIDA || foto().estado == FOTO_RECEBENDO;
}

inline const char* nomeEstadoFoto(uint8_t e) {
  static const char* const N[] = {"nenhuma", "pedida", "recebendo", "pronta", "erro"};
  return e <= FOTO_ERRO ? N[e] : "?";
}

inline void falhaFoto(const char* motivo) {
  Foto& f = foto();
  if (f.buf) {
    free(f.buf);
    f.buf = nullptr;
  }
  f.estado = FOTO_ERRO;
  f.erro   = motivo;
  Serial.printf("[foto] falhou: %s\n", motivo);
}

inline void envia(uint8_t tipo, const uint8_t* carga, uint8_t n) {
  uint8_t q[Enlace::QUADRO_MAX];
  const size_t t = Enlace::monta(tipo, carga, n, q, sizeof(q));
  if (t) {
    porta().write(q, t);
    fio().enviados++;
  }
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

inline void contaErroDaUart(hardwareSerial_error_t e) {
  if (e == UART_FIFO_OVF_ERROR)
    fio().errFifo++;
  else if (e == UART_BUFFER_FULL_ERROR)
    fio().errBuffer++;
  else
    fio().errQuadro++;
}

inline void abrePorta() {
  porta().onReceiveError(contaErroDaUart);
  // 4 kB de fila na recepcao, e nao os 256 B padrao. Durante uma foto os
  // bytes chegam a ~11 kB/s: 256 B enchem em 22 ms, e um unico redesenho
  // do OLED leva 25 ms a 400 kHz. Com a fila padrao, a primeira tela
  // desenhada no meio de uma foto perderia bytes e a foto inteira. 4 kB
  // dao ~350 ms de folga para o loop se atrasar sem perder nada.
  porta().setRxBufferSize(4096);
  porta().begin(ENLACE_BAUD, SERIAL_8N1, PIN_CAM_RX, PIN_CAM_TX);
  porta().setRxFIFOFull(ENLACE_FIFO_GATILHO);
}

// Teste eletrico do fio, sob demanda ('w' no console). Solta a UART, mede o
// que ha nos dois pinos e a reabre. Existe porque o defeito mais comum de
// montagem - TX e RX ligados um no outro - fecha a conta de um jeito
// enganoso: o vaso ouve os proprios pedidos e nao ha como saber, so pelo
// protocolo, se o curto e no C3, no fio ou dentro da camera.
// Roda uns 5 ms parado, e so quando alguem pede.
inline void testeDoFio() {
  porta().end();

  pinMode(PIN_CAM_TX, INPUT);  // solto: quem esta puxando o RX?
  pinMode(PIN_CAM_RX, INPUT_PULLDOWN);
  delay(2);
  const int comPulldown = digitalRead(PIN_CAM_RX);
  pinMode(PIN_CAM_RX, INPUT_PULLUP);
  delay(2);
  const int comPullup = digitalRead(PIN_CAM_RX);

  pinMode(PIN_CAM_RX, INPUT);  // sem pull: o que sobra e o que o TX empurra
  pinMode(PIN_CAM_TX, OUTPUT);
  digitalWrite(PIN_CAM_TX, HIGH);
  delayMicroseconds(500);
  const int emAlto = digitalRead(PIN_CAM_RX);
  digitalWrite(PIN_CAM_TX, LOW);
  delayMicroseconds(500);
  const int emBaixo = digitalRead(PIN_CAM_RX);
  pinMode(PIN_CAM_TX, INPUT);

  Serial.printf("\n  ------ teste do fio (GPIO%d = RX, GPIO%d = TX) ------\n", PIN_CAM_RX,
                PIN_CAM_TX);
  Serial.printf("    RX com TX solto:  pull-down -> %d   pull-up -> %d\n", comPulldown, comPullup);
  Serial.printf("    RX com TX em alto -> %d   TX em baixo -> %d\n", emAlto, emBaixo);
  if (emAlto == 1 && emBaixo == 0) {
    Serial.println(F("    RESULTADO: o RX SEGUE o TX. Os dois pinos estao ligados um no outro."));
    Serial.println(
        F("      Pode ser um jumper direto GPIO20-GPIO21, ou D0 e D1 da XIAO em curto."));
    Serial.println(
        F("      Tire os fios da XIAO e rode 'w' de novo: se continuar, o curto e do lado do C3."));
  } else if (comPulldown == 1) {
    Serial.println(F("    RESULTADO: fio limpo, e ha alguem empurrando o RX para alto:"));
    Serial.println(
        F("      a D0 da XIAO esta alimentada e ligada no GPIO20. Este e o estado bom."));
  } else if (comPullup == 1 && comPulldown == 0) {
    Serial.println(F("    RESULTADO: fio limpo, mas NINGUEM empurra o RX: ele so segue o pull."));
    Serial.println(F("      XIAO sem energia, ou D0 nao esta no GPIO20."));
  } else {
    Serial.println(F("    RESULTADO: leitura inconclusiva - RX preso em baixo. Curto para o GND?"));
  }

  abrePorta();
  receptor().reinicia();
  prazo() = 0;
  Serial.println(F("    (UART reaberta)"));
}

inline void begin() {
  memset(&V, 0, sizeof(V));
  V.classe = Visao::SEM_PLANTA;

  abrePorta();
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

inline Enlace::RemontaFoto& remonta() {
  static Enlace::RemontaFoto r;
  return r;
}

// Traduz a decisao da remontagem em efeito: pedido no fio, foto pronta ou
// falha. A politica (quando pedir, quando desistir) mora na biblioteca,
// onde o autoteste a exercita contra um fio simulado com perda.
inline void aplicaRemontagem(Enlace::RemontaFoto::Acao a) {
  Foto& f    = foto();
  f.recebido = remonta().recebido();
  f.reenvios = remonta().reenvios();

  switch (a) {
    case Enlace::RemontaFoto::PEDE_REENVIO: {
      uint8_t c[Enlace::FOTO_REENVIA_BYTES];
      Enlace::poe32(c, remonta().contiguo());
      Enlace::poe32(c + 4, remonta().total());
      envia(Enlace::TIPO_FOTO_REENVIA, c, sizeof(c));
      Serial.printf("[foto] reenvio %u: a partir do byte %lu de %lu\n", f.reenvios,
                    (unsigned long)remonta().contiguo(), (unsigned long)remonta().total());
      break;
    }
    case Enlace::RemontaFoto::PRONTA:
      f.crc       = Enlace::crc16(f.buf, f.total);
      f.estado    = FOTO_PRONTA;
      f.duracaoMs = millis() - f.pedidaEm;
      f.erro      = "";
      f.numero++;
      Serial.printf("[foto] pronta: %ux%u, %lu B em %lu ms, %u reenvios\n", f.largura, f.altura,
                    (unsigned long)f.total, (unsigned long)f.duracaoMs, f.reenvios);
      break;
    case Enlace::RemontaFoto::FALHOU: falhaFoto(remonta().motivo()); break;
    default: break;
  }
}

inline void trata(const Enlace::Quadro& q) {
  // Quadro que so o VASO envia, voltando para o vaso, e eco: TX ligado no
  // RX, ou a camera repetindo o que ouviu. Nao prova que ha camera do outro
  // lado - so que ha fio. Se contasse como resposta, o enlace ficaria "de
  // pe" sem veredito nenhum e sem uma unica falha, escondendo o defeito.
  if (q.tipo == Enlace::TIPO_PING || q.tipo == Enlace::TIPO_PEDE_VEREDITO ||
      q.tipo == Enlace::TIPO_PEDE_FOTO || q.tipo == Enlace::TIPO_CONFIG ||
      q.tipo == Enlace::TIPO_FOTO_REENVIA) {
    if (fio().ecos++ == 0) {
      Serial.println("[cam] ECO: o vaso ouviu o proprio quadro - TX e RX em curto, ou fio errado");
    }
    return;
  }

  switch (q.tipo) {
    case Enlace::TIPO_PONG: fio().pong++; break;
    case Enlace::TIPO_VEREDITO: fio().veredito++; break;
    case Enlace::TIPO_LOG: fio().logs++; break;
    default: fio().fotoQuadros++; break;
  }

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

    case Enlace::TIPO_FOTO_INICIO: {
      Foto& f = foto();
      // Aceita tambem em RECEBENDO: um INICIO no meio da recepcao e uma foto
      // nova (o pedido foi repetido e a camera tirou outra), e recomeca limpo.
      if ((f.estado != FOTO_PEDIDA && f.estado != FOTO_RECEBENDO) ||
          q.n < Enlace::FOTO_INICIO_BYTES)
        break;
      const uint32_t total = Enlace::pega32(q.carga);
      if (total == 0 || total > FOTO_MAX_BYTES) {
        falhaFoto("foto maior que FOTO_MAX_BYTES");
        break;
      }
      free(f.buf);
      f.buf = (uint8_t*)malloc(total);
      if (!f.buf) {
        falhaFoto("sem memoria para a foto");
        break;
      }
      f.total     = total;
      f.recebido  = 0;
      f.largura   = Enlace::pega16(q.carga + 4);
      f.altura    = Enlace::pega16(q.carga + 6);
      f.msCaptura = Enlace::pega16(q.carga + 8);
      f.estado    = FOTO_RECEBENDO;
      remonta().inicia(f.buf, total, millis());
      break;
    }

    case Enlace::TIPO_FOTO_PEDACO: {
      Foto& f = foto();
      if (f.estado != FOTO_RECEBENDO || q.n < 5) break;
      // Pedaco adiante do esperado quer dizer que um quadro morreu no CRC no
      // caminho. Remendar nao da - o buraco no meio do JPEG quebraria a
      // imagem sem aviso. A remontagem pede a camera para reenviar dali.
      aplicaRemontagem(remonta().pedaco(Enlace::pega32(q.carga), q.carga + 4, q.n - 4, millis()));
      break;
    }

    case Enlace::TIPO_FOTO_FIM: {
      Foto& f = foto();
      if (f.estado != FOTO_RECEBENDO || q.n < Enlace::FOTO_FIM_BYTES) break;
      aplicaRemontagem(
          remonta().fim(Enlace::pega32(q.carga), Enlace::pega16(q.carga + 4), millis()));
      break;
    }

    case Enlace::TIPO_FOTO_ERRO: {
      if (!fotoEmCurso()) break;
      const uint8_t cod = q.n ? q.carga[0] : 0;
      falhaFoto(cod == Enlace::FOTO_ERRO_SEM_CAMERA  ? "sensor da camera nao iniciou"
                : cod == Enlace::FOTO_ERRO_CAPTURA   ? "a camera nao conseguiu capturar"
                : cod == Enlace::FOTO_ERRO_SEM_COPIA ? "a camera nao guardou a foto para reenviar"
                                                     : "a camera devolveu um formato inesperado");
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

// Pedido vindo do app. Devolve nullptr se o pedido saiu (ou ja havia um
// em curso), ou o motivo da recusa.
inline const char* pedeFoto(bool bombaLigada) {
  if (fotoEmCurso()) return nullptr;
  if (!V.enlaceOk) return "camera sem enlace";
  if (!Energia::podeCapturar(bombaLigada)) return "bomba ligada - tente de novo em alguns segundos";

  Foto& f = foto();
  if (f.buf) {
    free(f.buf);  // a foto anterior sai da memoria; a pagina ja a mostrou
    f.buf = nullptr;
  }
  f.estado     = FOTO_PEDIDA;
  f.total      = 0;
  f.recebido   = 0;
  f.pedidaEm   = millis();
  f.erro       = "";
  f.reenvios   = 0;
  f.tentativas = 0;

  // Qualquer pergunta de veredito no ar e esquecida: a resposta ainda e
  // aceita se chegar, mas o atraso dela nao vai contar como falha.
  prazo() = 0;
  envia(Enlace::TIPO_PEDE_FOTO, nullptr, 0);
  Serial.println("[foto] pedida");
  return nullptr;
}

// Chamar todo loop. 'bombaLigada' entra por causa do orcamento de
// energia: nao se pede quadro com a bomba girando.
inline void tick(bool bombaLigada) {
  {
    // Quanto o loop principal demorou para voltar aqui. O servidor web e
    // sincrono e o OLED leva ~25 ms por quadro: se a soma passar do que o
    // buffer da UART aguenta, os bytes da foto caem no chao.
    const uint32_t t = millis();
    if (fio().ultimaVoltaEm) {
      const uint32_t volta = t - fio().ultimaVoltaEm;
      if (volta > fio().maiorVoltaMs) fio().maiorVoltaMs = volta;
    }
    fio().ultimaVoltaEm = t;
  }
  // ---- Recebe o que chegou -------------------------------------------
  // Esvazia o receptor a cada byte, nao so no fim. O receptor guarda UM
  // quadro; a foto chega em rajada e acumula dezenas de quadros na UART
  // enquanto o loop atende o HTTP. Empurrar tudo antes de tirar qualquer
  // quadro transbordava o receptor e perdia o primeiro pedaco - foi o que
  // o teste de campo de 21/09/2026 pegou ("pedaco perdido no fio").
  Enlace::Quadro q;
  while (porta().available()) {
    receptor().empurra((uint8_t)porta().read());
    fio().bytes++;
    fio().ultimoByteEm = millis();
    while (receptor().proximo(q)) trata(q);
  }

  const uint32_t agora = millis();

  // ---- Foto em curso: so vigia o prazo dela ---------------------------
  if (fotoEmCurso()) {
    Foto& f = foto();
    if (agora - f.pedidaEm > FOTO_TIMEOUT_MS) {
      falhaFoto(f.estado == FOTO_PEDIDA ? "a camera nao respondeu ao pedido"
                                        : "a foto parou no meio do caminho");
      proximaPergunta() = agora + 1000;
    } else if (f.estado == FOTO_PEDIDA) {
      // O proprio pedido pode ter morrido no fio, ou o INICIO na volta.
      // Repete ate duas vezes, espacado - a camera leva ~1,5 s para responder.
      if (f.tentativas < 2 && agora - f.pedidaEm > 3500UL * (f.tentativas + 1)) {
        f.tentativas++;
        envia(Enlace::TIPO_PEDE_FOTO, nullptr, 0);
        Serial.printf("[foto] sem resposta - pedido repetido (%u)\n", f.tentativas);
      }
    } else {
      aplicaRemontagem(remonta().parado(agora));
    }
    return;
  }

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
  // E so com a visao calibrada - ver VISAO_CALIBRADA em config.h.
  if (VISAO_CALIBRADA && V.enlaceOk && filtro().amostras() >= 4 && !V.temPlanta) {
    r |= RISCO_SEM_PLANTA;
  }
  return r;
}

}  // namespace Camera
