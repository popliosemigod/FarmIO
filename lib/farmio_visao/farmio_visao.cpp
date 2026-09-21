// =====================================================================
//  FarmIO - farmio_visao.cpp
//  Extracao das dez caracteristicas e a regressao logistica em cima
//  delas. C++11 puro, sem alocacao dinamica, sem ponto flutuante.
// =====================================================================
#include "farmio_visao.h"

#include <string.h>

namespace Visao {

// Buffers de trabalho. Estaticos de proposito: 9,4 kB reservados uma vez
// valem mais que um malloc por quadro, que na ESP32-CAM fragmenta a heap
// justo onde o driver da camera precisa de bloco contiguo.
static uint8_t g_exg[GRADE_N];
static uint8_t g_sat[GRADE_N];
static uint8_t g_cal[GRADE_N];

static const uint8_t FLAG_LUZ_BAIXA_V = 1 << 0;
static const uint8_t FLAG_ESTOURADO_V = 1 << 1;

Parametros Parametros::padrao() {
  Parametros p;
  p.pisoExg       = 40;
  p.brilhoMinimo  = 25;
  p.brilhoMaximo  = 245;
  p.blocoVerdePct = 35;
  p.limiarPlanta  = 650;
  p.limiarDuvida  = 350;
  return p;
}

// ---------------------------------------------------------------------
//  Sigmoide 1/(1+e^-z) tabelada em permil, z de -8 a +8 em 64 passos de
//  0,25. Fora dessa faixa a curva ja esta colada em 0 ou 1000 e nao ha o
//  que interpolar. Tabela e interpolacao linear custam ~20 ciclos; a
//  exponencial em software custaria centenas no C3, que nao tem FPU.
// ---------------------------------------------------------------------
static const uint16_t SIGMOIDE[65] = {
    0,   0,   1,   1,   1,   1,   2,   2,   2,   3,   4,   5,   7,    9,   11,  14,  18,
    23,  29,  37,  47,  60,  76,  95,  119, 148, 182, 223, 269, 321,  378, 438, 500, 562,
    622, 679, 731, 777, 818, 852, 881, 905, 924, 940, 953, 963, 971,  977, 982, 986, 989,
    991, 993, 995, 996, 997, 998, 998, 998, 999, 999, 999, 999, 1000, 1000};

uint16_t sigmoidePermil(int32_t zQ8) {
  if (zQ8 <= -2048) return 0;
  if (zQ8 >= 2048) return 1000;
  const int32_t desloc = zQ8 + 2048;   // 0 .. 4096
  const int32_t idx    = desloc >> 6;  // passo de 64 em Q8 = 0,25
  const int32_t frac   = desloc & 63;
  const int32_t a      = SIGMOIDE[idx];
  const int32_t b      = SIGMOIDE[idx >= 64 ? 64 : idx + 1];
  return (uint16_t)(a + ((b - a) * frac) / 64);
}

static inline int16_t limita(int32_t v) {
  if (v < 0) return 0;
  if (v > 1023) return 1023;
  return (int16_t)v;
}

// As escalas abaixo sao parte do modelo: mudar uma delas invalida os
// pesos treinados. Escolhidas para que uma cena tipica caia em 0..1,0
// (0..256 em Q8), que e a faixa onde a regressao converge rapido.
void normaliza(const Caracteristicas& c, int16_t x[N_CARACTERISTICAS]) {
  x[0] = limita(((int32_t)c.cobertura * 256) / 1000);
  x[1] = limita(((int32_t)c.exgMedio * 256) / 255);
  x[2] = limita(((int32_t)c.exgDesvio * 256) / 128);
  x[3] = limita(((int32_t)c.bordas * 256) / 128);
  x[4] = limita(((int32_t)c.saturacao * 256) / 255);
  x[5] = limita(((int32_t)c.brilho * 256) / 255);
  x[6] = limita(((int32_t)c.maiorRegiao * 256) / 1000);
  x[7] = limita(((int32_t)c.clusters * 256) / 8);
  x[8] = limita(((int32_t)c.perimetro * 256) / 1000);
  x[9] = limita(((int32_t)c.calor * 256) / 255);
}

uint16_t classifica(const Caracteristicas& c, const Pesos& p) {
  int16_t x[N_CARACTERISTICAS];
  normaliza(c, x);
  int32_t acc = 0;
  for (int i = 0; i < N_CARACTERISTICAS; i++) acc += (int32_t)p.w[i] * (int32_t)x[i];
  const int32_t zQ8 = (acc >> 8) + p.b;
  return sigmoidePermil(zQ8);
}

// ---------------------------------------------------------------------
//  Otsu: escolhe o limiar que maximiza a variancia entre as duas classes
//  do histograma. Uma passada de 256 posicoes, tudo inteiro.
// ---------------------------------------------------------------------
static uint8_t otsu(const uint32_t* hist, uint32_t total) {
  if (total == 0) return 0;
  uint64_t somaTotal = 0;
  for (int i = 0; i < 256; i++) somaTotal += (uint64_t)i * hist[i];

  uint64_t somaB  = 0;
  uint32_t pesoB  = 0;
  uint64_t melhor = 0;
  uint8_t limiar  = 0;

  for (int i = 0; i < 256; i++) {
    pesoB += hist[i];
    if (pesoB == 0) continue;
    const uint32_t pesoF = total - pesoB;
    if (pesoF == 0) break;
    somaB += (uint64_t)i * hist[i];

    const uint64_t mediaB = somaB / pesoB;
    const uint64_t mediaF = (somaTotal - somaB) / pesoF;
    const int64_t d       = (int64_t)mediaB - (int64_t)mediaF;
    const uint64_t entre  = (uint64_t)pesoB * pesoF * (uint64_t)(d * d);
    if (entre > melhor) {
      melhor = entre;
      limiar = (uint8_t)i;
    }
  }
  return limiar;
}

// Raiz quadrada inteira, para o desvio padrao. Newton converge em poucos
// passos para qualquer valor de 32 bits que aparece aqui.
static uint32_t raiz(uint32_t v) {
  if (v == 0) return 0;
  uint32_t x = v;
  uint32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}

bool extrai(const uint8_t* quadro, int larg, int alt, int passo, bool trocaBytes,
            const Parametros& par, Caracteristicas& fora) {
  memset(&fora, 0, sizeof(fora));
  if (!quadro || passo < 1) return false;

  const int gw = larg / passo;
  const int gh = alt / passo;
  if (gw < 8 || gh < 8 || gw > GRADE_LARG || gh > GRADE_ALT) return false;

  uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  uint32_t somaBrilho = 0;
  const int n         = gw * gh;

  // ---- Passada 1: cor -> ExG normalizado, saturacao e histograma -----
  for (int gy = 0; gy < gh; gy++) {
    const int y = gy * passo;
    for (int gx = 0; gx < gw; gx++) {
      const int x       = gx * passo;
      const size_t off  = ((size_t)y * larg + x) * 2;
      const uint8_t hi  = trocaBytes ? quadro[off] : quadro[off + 1];
      const uint8_t lo  = trocaBytes ? quadro[off + 1] : quadro[off];
      const uint16_t px = (uint16_t)(((uint16_t)hi << 8) | lo);

      // RGB565 -> 8 bits por canal. A regra de tres com 255 leva o branco
      // puro a 255 e nao a 248, que e o erro de quem so desloca bits.
      const uint8_t r = (uint8_t)((((px >> 11) & 0x1F) * 255) / 31);
      const uint8_t g = (uint8_t)((((px >> 5) & 0x3F) * 255) / 63);
      const uint8_t b = (uint8_t)(((px & 0x1F) * 255) / 31);

      const int32_t soma = (int32_t)r + (int32_t)g + (int32_t)b;
      somaBrilho += (uint32_t)(soma / 3);

      // ExG normalizado. A divisao pela soma cancela a intensidade da
      // luz: e o que faz a mesma folha valer o mesmo na sombra e no sol.
      int32_t exg = 0;
      if (soma > 24) {  // cena quase preta nao tem cromaticidade confiavel
        exg = (255 * (2 * (int32_t)g - (int32_t)r - (int32_t)b)) / soma;
        if (exg < 0) exg = 0;
        if (exg > 255) exg = 255;
      }

      const uint8_t mx  = r > g ? (r > b ? r : b) : (g > b ? g : b);
      const uint8_t mn  = r < g ? (r < b ? r : b) : (g < b ? g : b);
      const uint8_t sat = mx ? (uint8_t)((((int32_t)mx - (int32_t)mn) * 255) / mx) : 0;

      // Equilibrio entre vermelho e azul, centrado em 128.
      //
      // Esta e a caracteristica que separa folha amarelada de plastico
      // verde, e ela vem da fisica do pigmento: a clorofila absorve o
      // azul com mais forca que o vermelho, entao vegetacao - viva ou
      // senescente - reflete mais vermelho que azul. Corante verde de
      // plastico e de tecido e ciano-deslocado e faz o contrario. Sem
      // ela, folha estressada e balde verde caem no mesmo lugar do
      // espaco de caracteristicas.
      int32_t cal = 128;
      if (soma > 24) cal = 128 + (255 * ((int32_t)r - (int32_t)b)) / (2 * soma);
      if (cal < 0) cal = 0;
      if (cal > 255) cal = 255;

      const int i = gy * gw + gx;
      g_exg[i]    = (uint8_t)exg;
      g_sat[i]    = sat;
      g_cal[i]    = (uint8_t)cal;
      hist[exg]++;
    }
  }

  fora.brilho = (uint8_t)(somaBrilho / (uint32_t)n);
  if (fora.brilho < par.brilhoMinimo) fora.flags |= FLAG_LUZ_BAIXA_V;
  if (fora.brilho > par.brilhoMaximo) fora.flags |= FLAG_ESTOURADO_V;

  // ---- Limiar: Otsu, com piso ----------------------------------------
  uint8_t limiar = otsu(hist, (uint32_t)n);
  if (limiar < par.pisoExg) limiar = par.pisoExg;
  fora.limiarUsado = limiar;

  // ---- Passada 2: estatisticas dentro da mascara ---------------------
  uint32_t area = 0, somaExg = 0, somaSat = 0, somaGrad = 0, perim = 0, somaCal = 0;
  uint32_t nGrad = 0;

  for (int gy = 0; gy < gh; gy++) {
    for (int gx = 0; gx < gw; gx++) {
      const int i = gy * gw + gx;
      if (g_exg[i] < limiar) continue;
      area++;
      somaExg += g_exg[i];
      somaSat += g_sat[i];
      somaCal += g_cal[i];

      if (gx + 1 < gw && gy + 1 < gh) {
        const int dx = (int)g_exg[i + 1] - (int)g_exg[i];
        const int dy = (int)g_exg[i + gw] - (int)g_exg[i];
        somaGrad += (uint32_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
        nGrad++;
      }

      // Perimetro: amostra verde com vizinho nao-verde, ou na borda do
      // quadro. Superficie verde lisa e grande tem perimetro pequeno em
      // relacao a area; folhagem, recortada, tem perimetro enorme.
      const bool borda =
          (gx == 0 || g_exg[i - 1] < limiar) || (gx == gw - 1 || g_exg[i + 1] < limiar) ||
          (gy == 0 || g_exg[i - gw] < limiar) || (gy == gh - 1 || g_exg[i + gw] < limiar);
      if (borda) perim++;
    }
  }

  fora.cobertura = (uint16_t)(((uint32_t)area * 1000) / (uint32_t)n);
  uint32_t media = 0;
  if (area) {
    media          = somaExg / area;
    fora.exgMedio  = (uint8_t)media;
    fora.saturacao = (uint8_t)(somaSat / area);
    fora.calor     = (uint8_t)(somaCal / area);
    uint32_t pr    = ((uint32_t)perim * 1000) / area;
    fora.perimetro = (uint16_t)(pr > 1000 ? 1000 : pr);
  }

  // TEXTURA RELATIVA, NAO ABSOLUTA.
  //
  // Este divisor e a correcao mais importante do modulo, e ela saiu de um
  // erro medido: com bordas e desvio em unidades absolutas de ExG, a
  // folhagem AMARELADA acertava 5 de 24. O motivo nao era a cor dela ser
  // esquisita - era que ExG de folha clorotica vale ~84 contra ~168 de
  // folha sadia, e toda a textura medida em cima dele encolhe na mesma
  // proporcao. O classificador entao lia "folha amarelada tem metade da
  // textura de uma folha", que e falso: ela tem a MESMA textura, num
  // sinal de metade da amplitude.
  //
  // Dividir pela media dentro da mascara torna as duas medidas
  // invariantes a escala - exatamente o que a divisao pela soma dos
  // canais ja fazia com a cor, um nivel acima. Depois disso, folha sadia
  // e folha clorotica dao o mesmo numero de bordas (~72), e plastico
  // verde continua em ~20.
  if (nGrad && media) {
    uint32_t g  = ((somaGrad / nGrad) * 255) / media;
    fora.bordas = (uint8_t)(g > 255 ? 255 : g);
  }

  // Desvio padrao do ExG na mascara: a medida de textura. Folha tem
  // nervura e sombra propria, entao espalha; plastico verde e quase
  // constante e o desvio cai perto de zero.
  if (area > 1) {
    uint64_t soma2 = 0;
    for (int i = 0; i < n; i++) {
      if (g_exg[i] < limiar) continue;
      const int32_t d = (int32_t)g_exg[i] - (int32_t)media;
      soma2 += (uint64_t)((int64_t)d * (int64_t)d);
    }
    const uint32_t var = (uint32_t)(soma2 / area);
    const uint32_t dp  = media ? (raiz(var) * 255) / media : 0;
    fora.exgDesvio     = (uint8_t)(dp > 255 ? 255 : dp);
  }

  // ---- Forma: aglomerados na grade grossa de blocos -------------------
  uint8_t bloco[BLOCO_N];
  uint16_t contaBloco[BLOCO_N];
  uint16_t totalBloco[BLOCO_N];
  memset(contaBloco, 0, sizeof(contaBloco));
  memset(totalBloco, 0, sizeof(totalBloco));

  for (int gy = 0; gy < gh; gy++) {
    const int by = (gy * BLOCO_ALT) / gh;
    for (int gx = 0; gx < gw; gx++) {
      const int bx = (gx * BLOCO_LARG) / gw;
      const int bi = by * BLOCO_LARG + bx;
      totalBloco[bi]++;
      if (g_exg[gy * gw + gx] >= limiar) contaBloco[bi]++;
    }
  }
  for (int i = 0; i < BLOCO_N; i++) {
    const bool cheio =
        totalBloco[i] && ((uint32_t)contaBloco[i] * 100u) / totalBloco[i] >= par.blocoVerdePct;
    bloco[i] = cheio ? 1 : 0;
  }

  // Rotulagem por inundacao, 4-conexo, com pilha propria: BLOCO_N vale
  // 80, entao a pilha cabe na pilha de chamada sem risco de estouro.
  uint8_t visto[BLOCO_N];
  memset(visto, 0, sizeof(visto));
  int pilha[BLOCO_N];
  uint16_t maior    = 0;
  uint8_t nClusters = 0;

  for (int inicio = 0; inicio < BLOCO_N; inicio++) {
    if (!bloco[inicio] || visto[inicio]) continue;
    int topo         = 0;
    pilha[topo++]    = inicio;
    visto[inicio]    = 1;
    uint16_t tamanho = 0;

    while (topo > 0) {
      const int cur = pilha[--topo];
      tamanho++;
      const int bx     = cur % BLOCO_LARG;
      const int by     = cur / BLOCO_LARG;
      const int viz[4] = {bx > 0 ? cur - 1 : -1, bx < BLOCO_LARG - 1 ? cur + 1 : -1,
                          by > 0 ? cur - BLOCO_LARG : -1,
                          by < BLOCO_ALT - 1 ? cur + BLOCO_LARG : -1};
      for (int k = 0; k < 4; k++) {
        const int v = viz[k];
        if (v < 0 || visto[v] || !bloco[v]) continue;
        visto[v]      = 1;
        pilha[topo++] = v;
      }
    }
    if (tamanho > maior) maior = tamanho;
    if (nClusters < 255) nClusters++;
  }

  fora.maiorRegiao = (uint16_t)(((uint32_t)maior * 1000) / BLOCO_N);
  fora.clusters    = nClusters;
  return true;
}

bool avalia(const uint8_t* quadro, int larg, int alt, int passo, bool trocaBytes,
            const Parametros& par, const Pesos& pesos, Veredito& fora) {
  memset(&fora, 0, sizeof(fora));
  if (!extrai(quadro, larg, alt, passo, trocaBytes, par, fora.c)) return false;

  fora.probabilidade = classifica(fora.c, pesos);

  // Luz baixa nao vira "sem planta": vira duvida. A camera nao enxergar
  // nao e prova de que o vaso esta vazio, e tratar como prova seria
  // exatamente o erro que bloqueia a irrigacao de madrugada.
  if (fora.c.flags & FLAG_LUZ_BAIXA_V) {
    fora.classe = PROVAVEL;
    return true;
  }
  if (fora.probabilidade >= par.limiarPlanta) {
    fora.classe = PLANTA;
  } else if (fora.probabilidade >= par.limiarDuvida) {
    fora.classe = PROVAVEL;
  } else {
    fora.classe = SEM_PLANTA;
  }
  return true;
}

// ---------------------------------------------------------------------
Filtro::Filtro() {
  reinicia();
}

void Filtro::reinicia() {
  memset(hist_, 0, sizeof(hist_));
  pos_       = 0;
  usadas_    = 0;
  temPlanta_ = false;
}

uint16_t Filtro::media() const {
  if (!usadas_) return 0;
  uint32_t s = 0;
  for (uint8_t i = 0; i < usadas_; i++) s += hist_[i];
  return (uint16_t)(s / usadas_);
}

void Filtro::empurra(uint16_t p) {
  hist_[pos_] = p;
  pos_        = (uint8_t)((pos_ + 1) % JANELA);
  if (usadas_ < JANELA) usadas_++;

  // Histerese: sobe em 600 permil de media, so desce abaixo de 400. Sem
  // a janela morta, o veredito ficaria batendo em torno do limiar toda
  // vez que uma nuvem passasse na frente do sol.
  const uint16_t m = media();
  if (!temPlanta_ && m >= 600) temPlanta_ = true;
  if (temPlanta_ && m < 400) temPlanta_ = false;
}

}  // namespace Visao
