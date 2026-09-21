// =====================================================================
//  FarmIO - cenas.cpp
//  Desenho das cenas sinteticas. Compilado SO no ambiente `autoteste`
//  (ver build_src_filter no platformio.ini): nao ocupa um byte da flash
//  do vaso nem da camera.
//
//  Cada cena e uma receita de tres partes - cor de fundo, estrutura e
//  ruido - com todos os tres sorteados dentro de uma faixa. A faixa e
//  larga de proposito: se as classes ficassem perfeitamente separadas, o
//  acerto de 100% mediria a ingenuidade do gerador, nao a qualidade do
//  classificador.
// =====================================================================
#include "cenas.h"

#include <math.h>
#include <string.h>

namespace Cenas {

// ---------------------------------------------------------------------
//  Sorteio reproduzivel. Xorshift32: dez linhas, periodo de 4 bilhoes,
//  e o mesmo resultado em qualquer maquina - o que importa aqui e que o
//  banco de treino de hoje seja identico ao de amanha.
// ---------------------------------------------------------------------
struct Rng {
  uint32_t s;

  uint32_t proximo() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int entre(int a, int b) {
    if (b <= a) return a;
    return a + (int)(proximo() % (uint32_t)(b - a + 1));
  }
  float f() { return (float)(proximo() & 0xFFFFFF) / (float)0x1000000; }
  float entref(float a, float b) { return a + (b - a) * f(); }
};

static inline int trava(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// RGB565 com o byte alto primeiro - mesma ordem que a esp32-camera
// entrega, para que treino e operacao vejam o mesmo pixel.
static inline void poe(uint8_t* q, int x, int y, int r, int g, int b) {
  if (x < 0 || y < 0 || x >= LARGURA || y >= ALTURA) return;
  r                 = trava(r, 0, 255);
  g                 = trava(g, 0, 255);
  b                 = trava(b, 0, 255);
  const uint16_t px = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  const size_t off  = ((size_t)y * LARGURA + x) * 2;
  q[off]            = (uint8_t)(px >> 8);
  q[off + 1]        = (uint8_t)(px & 0xFF);
}

// ---------------------------------------------------------------------
//  Fundo chapado com gradiente de iluminacao e ruido de sensor.
//
//  O gradiente existe porque nenhuma cena real e iluminada por igual, e
//  o ruido porque o sensor da OV2640 sempre entrega alguns niveis de
//  granulacao. Sem os dois, o desvio padrao das cenas lisas cairia a
//  zero e o classificador acharia que separar folha de plastico e
//  trivial - descobrindo o contrario so na bancada.
// ---------------------------------------------------------------------
static void fundo(uint8_t* q, Rng& r, int cr, int cg, int cb, int ruido, float gradiente) {
  const float gx = r.entref(-1.0f, 1.0f);
  const float gy = r.entref(-1.0f, 1.0f);
  for (int y = 0; y < ALTURA; y++) {
    for (int x = 0; x < LARGURA; x++) {
      const float fx = (float)x / LARGURA - 0.5f;
      const float fy = (float)y / ALTURA - 0.5f;
      const float k  = 1.0f + gradiente * (gx * fx + gy * fy);
      const int n    = ruido ? r.entre(-ruido, ruido) : 0;
      poe(q, x, y, (int)(cr * k) + n, (int)(cg * k) + n, (int)(cb * k) + n);
    }
  }
}

// Granulos de terra: manchas de um a tres pixels, mais claras e mais
// escuras que o fundo. E o que da a terra a textura que ela realmente
// tem - terra chapada seria facil demais de descartar.
static void granulos(uint8_t* q, Rng& r, int cr, int cg, int cb, int quantos, int amplitude) {
  for (int i = 0; i < quantos; i++) {
    const int x = r.entre(0, LARGURA - 1);
    const int y = r.entre(0, ALTURA - 1);
    const int d = r.entre(-amplitude, amplitude);
    const int t = r.entre(0, 2);
    for (int dy = 0; dy <= t; dy++) {
      for (int dx = 0; dx <= t; dx++) poe(q, x + dx, y + dy, cr + d, cg + d, cb + d);
    }
  }
}

// ---------------------------------------------------------------------
//  Uma folha: elipse girada, com nervura central, sombreamento nas
//  bordas e um ponto especular. Sao esses quatro detalhes - e nao a cor
//  - que produzem o desvio padrao e o gradiente que separam folhagem de
//  superficie verde lisa.
// ---------------------------------------------------------------------
static void folha(uint8_t* q, Rng& r, int cx, int cy, int rx, int ry, int fr, int fg, int fb,
                  int ruido) {
  const float ang = r.entref(0.0f, 3.14159f);
  const float ca = cosf(ang), sa = sinf(ang);
  const float espec = r.entref(0.0f, 1.0f) < 0.6f ? r.entref(0.15f, 0.45f) : 0.0f;
  const int raio    = (rx > ry ? rx : ry) + 1;

  for (int dy = -raio; dy <= raio; dy++) {
    for (int dx = -raio; dx <= raio; dx++) {
      const float u = (dx * ca + dy * sa) / (float)rx;
      const float v = (-dx * sa + dy * ca) / (float)ry;
      const float d = u * u + v * v;
      if (d > 1.0f) continue;

      // Sombreamento: a folha e mais escura na borda que no centro.
      float k = 1.0f - 0.40f * d;
      // Nervura central, mais escura que o limbo.
      if (v > -0.07f && v < 0.07f) k *= 0.78f;
      // Brilho especular fora do centro, como reflexo de luz difusa.
      if (espec > 0.0f) {
        const float du = u + 0.35f, dv = v + 0.35f;
        const float e = du * du + dv * dv;
        if (e < 0.20f) k += espec * (0.20f - e) / 0.20f;
      }
      const int n = ruido ? r.entre(-ruido, ruido) : 0;
      poe(q, cx + dx, cy + dy, (int)(fr * k) + n, (int)(fg * k) + n, (int)(fb * k) + n);
    }
  }
}

static void folhagem(uint8_t* q, Rng& r, int quantas, int rmin, int rmax, int baseR, int baseG,
                     int baseB, float luz, int ruido) {
  for (int i = 0; i < quantas; i++) {
    const int cx = r.entre(-10, LARGURA + 10);
    const int cy = r.entre(-8, ALTURA + 8);
    const int rx = r.entre(rmin, rmax);
    const int ry = r.entre(rmin / 2 > 3 ? rmin / 2 : 3, rx);
    // Cada folha tem a propria idade e a propria exposicao ao sol.
    const float j = r.entref(0.72f, 1.28f) * luz;
    folha(q, r, cx, cy, rx, ry, (int)(baseR * j), (int)(baseG * j), (int)(baseB * j), ruido);
  }
}

// ---------------------------------------------------------------------
uint8_t rotulo(uint8_t tipo) {
  switch (tipo) {
    case FOLHAGEM_DENSA:
    case FOLHAGEM_ESPARSA:
    case MUDA_PEQUENA:
    case FOLHAGEM_SOMBRA:
    case FOLHAGEM_AMARELADA: return 1;
    case ESCURO: return 255;  // sem luz nao ha o que julgar
    default: return 0;
  }
}

const char* nome(uint8_t tipo) {
  switch (tipo) {
    case FOLHAGEM_DENSA: return "folhagem densa";
    case FOLHAGEM_ESPARSA: return "folhagem esparsa";
    case MUDA_PEQUENA: return "muda pequena";
    case FOLHAGEM_SOMBRA: return "folhagem na sombra";
    case FOLHAGEM_AMARELADA: return "folhagem amarelada";
    case SOLO_SECO: return "solo seco";
    case SOLO_UMIDO: return "solo umido";
    case MADEIRA: return "bancada de madeira";
    case PAREDE_BRANCA: return "parede branca";
    case PANO_VERDE: return "pano verde liso";
    case PLASTICO_VERDE: return "plastico verde";
    case CEU: return "ceu pela janela";
    case ESCURO: return "escuro";
    default: return "?";
  }
}

void desenha(uint8_t tipo, uint32_t semente, uint8_t* q) {
  Rng r;
  r.s = semente ? semente : 1u;  // xorshift morre no zero
  for (int i = 0; i < 8; i++) r.proximo();
  memset(q, 0, BYTES);

  switch (tipo) {
    case FOLHAGEM_DENSA: {
      const float luz = r.entref(0.85f, 1.15f);
      fundo(q, r, 38, 34, 28, r.entre(3, 8), 0.25f);
      folhagem(q, r, r.entre(34, 55), 10, 26, 62, 132, 52, luz, r.entre(4, 10));
      break;
    }

    case FOLHAGEM_ESPARSA: {
      const float luz = r.entref(0.85f, 1.15f);
      fundo(q, r, 118, 92, 66, r.entre(6, 14), 0.30f);
      granulos(q, r, 118, 92, 66, r.entre(500, 1400), r.entre(18, 34));
      folhagem(q, r, r.entre(8, 16), 9, 20, 64, 136, 54, luz, r.entre(4, 10));
      break;
    }

    case MUDA_PEQUENA: {
      const float luz = r.entref(0.85f, 1.15f);
      fundo(q, r, 112, 88, 64, r.entre(6, 14), 0.30f);
      granulos(q, r, 112, 88, 64, r.entre(600, 1500), r.entre(18, 34));
      // Poucas folhas pequenas, agrupadas no centro: e o caso em que
      // cobertura sozinha diria "sem planta".
      for (int i = 0, n = r.entre(3, 7); i < n; i++) {
        const int cx  = LARGURA / 2 + r.entre(-16, 16);
        const int cy  = ALTURA / 2 + r.entre(-12, 12);
        const int rx  = r.entre(5, 11);
        const float j = r.entref(0.75f, 1.25f) * luz;
        folha(q, r, cx, cy, rx, r.entre(3, rx), (int)(66 * j), (int)(140 * j), (int)(56 * j),
              r.entre(4, 9));
      }
      break;
    }

    case FOLHAGEM_SOMBRA: {
      // Mesma planta, um terco da luz. O indice ExG e normalizado pela
      // soma dos canais justamente para sobreviver a isto.
      const float luz = r.entref(0.28f, 0.42f);
      fundo(q, r, 16, 15, 13, r.entre(3, 7), 0.25f);
      folhagem(q, r, r.entre(28, 46), 10, 26, 62, 132, 52, luz, r.entre(3, 7));
      break;
    }

    case FOLHAGEM_AMARELADA: {
      // Planta estressada: clorose. Continua sendo planta, e o modelo
      // que so aceita verde-escuro perde exatamente a que precisa de
      // atencao.
      const float luz = r.entref(0.85f, 1.15f);
      fundo(q, r, 96, 78, 56, r.entre(5, 12), 0.28f);
      folhagem(q, r, r.entre(26, 44), 10, 24, 138, 152, 58, luz, r.entre(5, 11));
      break;
    }

    case SOLO_SECO:
      fundo(q, r, r.entre(135, 165), r.entre(105, 130), r.entre(72, 96), r.entre(8, 18), 0.35f);
      granulos(q, r, 150, 118, 84, r.entre(1500, 3500), r.entre(20, 40));
      break;

    case SOLO_UMIDO:
      fundo(q, r, r.entre(62, 88), r.entre(48, 66), r.entre(36, 50), r.entre(6, 14), 0.35f);
      granulos(q, r, 72, 56, 42, r.entre(1200, 3000), r.entre(14, 28));
      break;

    case MADEIRA: {
      fundo(q, r, r.entre(140, 170), r.entre(98, 122), r.entre(58, 78), r.entre(4, 10), 0.22f);
      // Veio da madeira: listras horizontais suaves.
      const float per = r.entref(6.0f, 16.0f);
      const float amp = r.entref(10.0f, 26.0f);
      for (int y = 0; y < ALTURA; y++) {
        const int d = (int)(amp * sinf((float)y / per + r.f()));
        for (int x = 0; x < LARGURA; x++) {
          const int n = r.entre(-4, 4);
          poe(q, x, y, 155 + d + n, 110 + d + n, 68 + d + n);
        }
      }
      break;
    }

    case PAREDE_BRANCA:
      fundo(q, r, r.entre(205, 240), r.entre(205, 240), r.entre(200, 236), r.entre(3, 9), 0.18f);
      break;

    case PANO_VERDE: {
      // Negativo dificil numero um: verde saturado cobrindo o quadro
      // inteiro. Cor diz planta; textura diz que nao.
      const int ruido = r.entre(3, 12);
      fundo(q, r, r.entre(48, 72), r.entre(132, 170), r.entre(56, 84), ruido, 0.22f);
      // Trama do tecido: uma modulacao fina e regular, que sobe um pouco
      // o desvio sem produzir borda nenhuma.
      for (int y = 0; y < ALTURA; y += 2) {
        for (int x = 0; x < LARGURA; x++) {
          const int d = ((x + y) & 1) ? 4 : -4;
          poe(q, x, y, 60 + d, 150 + d, 68 + d);
        }
      }
      break;
    }

    case PLASTICO_VERDE: {
      // Negativo dificil numero dois: plastico brilhante tem ExG MAIOR
      // que folha de verdade, porque reflete verde puro sem a sombra
      // interna que a folha tem.
      const int ruido = r.entre(2, 9);
      fundo(q, r, r.entre(36, 58), r.entre(148, 186), r.entre(48, 74), ruido, 0.30f);
      // Reflexo largo e liso, tipico de superficie curva e polida.
      const int hx = r.entre(30, LARGURA - 30), hy = r.entre(20, ALTURA - 20);
      const float raio = r.entref(28.0f, 60.0f);
      for (int y = 0; y < ALTURA; y++) {
        for (int x = 0; x < LARGURA; x++) {
          const float d = sqrtf((float)((x - hx) * (x - hx) + (y - hy) * (y - hy)));
          if (d > raio) continue;
          const float k = 1.0f + 0.55f * (1.0f - d / raio);
          poe(q, x, y, (int)(46 * k), (int)(166 * k), (int)(60 * k));
        }
      }
      break;
    }

    case CEU: {
      const int topoR = r.entre(96, 130), topoG = r.entre(140, 172), topoB = r.entre(198, 235);
      for (int y = 0; y < ALTURA; y++) {
        const float t = (float)y / ALTURA;
        for (int x = 0; x < LARGURA; x++) {
          const int n = r.entre(-4, 4);
          poe(q, x, y, (int)(topoR + 70 * t) + n, (int)(topoG + 55 * t) + n,
              (int)(topoB + 20 * t) + n);
        }
      }
      for (int i = 0, n = r.entre(1, 4); i < n; i++) {
        const int cx = r.entre(0, LARGURA), cy = r.entre(0, ALTURA / 2);
        const int rx = r.entre(20, 50);
        folha(q, r, cx, cy, rx, r.entre(8, 20), 240, 242, 246, 3);
      }
      break;
    }

    case ESCURO:
    default:
      fundo(q, r, r.entre(4, 14), r.entre(4, 15), r.entre(4, 14), r.entre(2, 6), 0.40f);
      break;
  }
}

}  // namespace Cenas
