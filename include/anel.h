// =====================================================================
//  FarmIO - anel.h
//  Anel de 16 LEDs WS2812. E o aviso que se ve do outro lado da sala,
//  sem precisar ler a tela.
//
//  Linguagem de cor, direto da especificacao do SmartFarm:
//    verde girando  - ligando
//    branco fixo    - funcionamento padrao
//    vermelho fixo  - situacao de risco
//    vermelho->branco girando - risco resolvido, voltando ao normal
//
//  O pedido explicito era "que a LED nao gere desconforto visual": todas
//  as transicoes sao lentas e o brilho e baixo. Por isso nao ha pisca-
//  pisca em lugar nenhum - o anel muda de estado deslizando, nao piscando.
// =====================================================================
#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"
#include "energia.h"

namespace Anel {

enum Modo : uint8_t {
  LIGANDO = 0,
  NORMAL,
  RISCO,
  RESOLVENDO
};

inline Adafruit_NeoPixel& tira() {
  static Adafruit_NeoPixel instancia(ANEL_PIXELS, PIN_ANEL, NEO_GRB + NEO_KHZ800);
  return instancia;
}

inline Modo& modo() {
  static Modo m = LIGANDO;
  return m;
}

inline uint32_t& marcaDoModo() {
  static uint32_t t = 0;
  return t;
}

inline void begin() {
  tira().begin();
  // Comeca no escuro: o brilho de regime e calculado no primeiro tick, a
  // partir do orcamento de energia. Acender em ANEL_BRILHO aqui seria
  // pedir 150 mA no instante em que a camera tambem esta bootando - o
  // pico coincidente que derruba a porta USB.
  tira().setBrightness(0);
  tira().clear();
  tira().show();
  marcaDoModo() = millis();
}

// O brilho nao e constante: e o que sobra do orcamento depois de pagar o
// C3, a camera, o display e o sensor. Recalculado a cada quadro porque a
// camera entra e sai do orcamento conforme o enlace vive ou morre.
inline uint8_t& brilhoAtual() {
  static uint8_t b = 0;
  return b;
}

inline void ajustaBrilho(bool cameraViva, bool bombaLigada) {
  const uint8_t alvo = Energia::brilhoPermitido(cameraViva, bombaLigada);
  if (alvo == brilhoAtual()) return;
  // Um degrau por quadro (25 fps): a mudanca de brilho vira uma rampa de
  // fracao de segundo em vez de um salto. O pedido era que a LED nao
  // gerasse desconforto visual, e salto de brilho e desconforto.
  brilhoAtual() =
      alvo > brilhoAtual() ? (uint8_t)(brilhoAtual() + 1) : (uint8_t)(brilhoAtual() - 1);
  tira().setBrightness(brilhoAtual());
}

inline void setModo(Modo m) {
  if (modo() == m) return;  // nao reinicia a animacao a toa
  modo()        = m;
  marcaDoModo() = millis();
}

// Interpolacao linear entre duas cores, componente a componente.
inline uint32_t mistura(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2,
                        float f) {
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return Adafruit_NeoPixel::Color((uint8_t)(r1 + (r2 - r1) * f), (uint8_t)(g1 + (g2 - g1) * f),
                                  (uint8_t)(b1 + (b2 - b1) * f));
}

// Chamar todo loop. Redesenha no maximo a cada 40 ms (25 fps): mais que
// isso nao muda nada para o olho e so ocupa o barramento.
inline void tick() {
  static uint32_t ultimoDesenho = 0;
  const uint32_t agora          = millis();
  if (agora - ultimoDesenho < 40) return;
  ultimoDesenho = agora;

  const uint32_t dt    = agora - marcaDoModo();
  Adafruit_NeoPixel& t = tira();

  switch (modo()) {
    case LIGANDO: {
      // Cometa verde dando a volta: 2 s por volta, com rastro de 5 pixels.
      const float volta = (dt % 2000) / 2000.0f;
      const int cabeca  = (int)(volta * ANEL_PIXELS);
      for (int i = 0; i < ANEL_PIXELS; i++) {
        int atras    = (cabeca - i + ANEL_PIXELS) % ANEL_PIXELS;
        float brilho = atras < 5 ? (1.0f - atras / 5.0f) : 0.0f;
        t.setPixelColor(i, t.Color(0, (uint8_t)(180 * brilho), (uint8_t)(40 * brilho)));
      }
      break;
    }

    case NORMAL: {
      // Branco fixo. A entrada e suave: 1,5 s subindo do apagado.
      const float f    = dt < 1500 ? dt / 1500.0f : 1.0f;
      const uint32_t c = t.Color((uint8_t)(200 * f), (uint8_t)(200 * f), (uint8_t)(190 * f));
      for (int i = 0; i < ANEL_PIXELS; i++) t.setPixelColor(i, c);
      break;
    }

    case RISCO: {
      // Vermelho com respiracao muito lenta (4 s por ciclo). Chama
      // atencao sem o desconforto de um pisca.
      const float fase = (dt % 4000) / 4000.0f;
      const float f    = 0.55f + 0.45f * sinf(fase * TWO_PI);
      const uint32_t c = t.Color((uint8_t)(220 * f), 0, 0);
      for (int i = 0; i < ANEL_PIXELS; i++) t.setPixelColor(i, c);
      break;
    }

    case RESOLVENDO: {
      // Volta ao normal: a transicao vermelho->branco percorre o anel em
      // 2,5 s, e ao fim o modo vira NORMAL sozinho.
      const float p    = dt / 2500.0f;
      const int limite = (int)(p * ANEL_PIXELS);
      for (int i = 0; i < ANEL_PIXELS; i++) {
        t.setPixelColor(i, i <= limite ? mistura(220, 0, 0, 200, 200, 190, 1.0f)
                                       : mistura(220, 0, 0, 200, 200, 190, 0.0f));
      }
      if (p >= 1.0f) setModo(NORMAL);
      break;
    }
  }

  t.show();
}

// Traduz o estado do vaso em modo do anel. Chamado quando o conjunto de
// riscos muda.
inline void reflete(uint8_t riscos, bool aindaLigando) {
  if (aindaLigando) {
    setModo(LIGANDO);
    return;
  }
  if (riscos != RISCO_NENHUM) {
    setModo(RISCO);
    return;
  }
  if (modo() == RISCO) {
    setModo(RESOLVENDO);
    return;
  }
  if (modo() == LIGANDO) setModo(NORMAL);
}

}  // namespace Anel
