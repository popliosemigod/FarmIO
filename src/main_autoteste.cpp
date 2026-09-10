// =====================================================================
//  FarmIO - main_autoteste.cpp
//  Bancada de ensaio que roda DENTRO da placa, sem sensor, sem camera e
//  sem fio nenhum ligado. Faz tres coisas:
//
//    1. exercita o protocolo do enlace contra os quatro modos de falha
//       que ele vai encontrar de verdade no fio;
//    2. gera o banco de cenas sinteticas, extrai as nove caracteristicas
//       e TREINA a regressao logistica na propria placa;
//    3. mede acerto, tempo por quadro e imprime os pesos prontos para
//       colar em lib/farmio_visao/pesos.cpp.
//
//  POR QUE TREINAR NA PLACA E NAO NO PC. Porque nao ha compilador de
//  host nesta maquina, e porque treinar onde se executa elimina de uma
//  vez a classe de erro mais chata deste tipo de trabalho: o modelo que
//  acerta no notebook e erra no microcontrolador porque a extracao de
//  caracteristica divergiu entre as duas implementacoes. Aqui so existe
//  uma implementacao, e e a que vai para o campo.
//
//  Gravar e ler:
//      pio run -e autoteste -t upload
//      pio device monitor -e autoteste
// =====================================================================
#include <Arduino.h>

#include "farmio_enlace.h"
#include "farmio_visao.h"
#include "cenas.h"

// Sementes por tipo de cena. As primeiras SEMENTES_TREINO vao para o
// treino, o resto e reservado para a medida - misturar os dois daria um
// numero bonito e mentiroso.
static const int SEMENTES_TOTAL  = 24;
static const int SEMENTES_TREINO = 16;

static const int MAX_AMOSTRAS = Cenas::N_TIPOS * SEMENTES_TOTAL;

struct Amostra {
  int16_t x[Visao::N_CARACTERISTICAS];  // Q8
  uint8_t tipo;
  uint8_t rotulo;  // 0 ou 1
  bool treino;
};

static Amostra* g_banco       = nullptr;
static int g_n                = 0;
static uint32_t g_usPorQuadro = 0;

// ---------------------------------------------------------------------
//  1. Enlace
// ---------------------------------------------------------------------
static int g_falhas = 0;

static void confere(const char* nome, bool ok) {
  Serial.printf("  [%s] %s\n", ok ? "ok " : "FALHOU", nome);
  if (!ok) g_falhas++;
}

static void testaEnlace() {
  Serial.println(F("\n--- 1. enlace serial C3 <-> ESP32-CAM ---"));

  uint8_t quadro[Enlace::QUADRO_MAX];
  Enlace::Receptor rec;
  Enlace::Quadro q;

  // Ida e volta de um veredito completo.
  Enlace::CargaVeredito v;
  v.probabilidade  = 873;
  v.cobertura      = 642;
  v.exgMedio       = 151;
  v.maiorRegiaoPct = 62;
  v.clusters       = 3;
  v.ms             = 41;
  v.classe         = Visao::PLANTA;
  v.flags          = 0;

  uint8_t carga[Enlace::VEREDITO_BYTES];
  Enlace::serializa(v, carga);
  size_t n =
      Enlace::monta(Enlace::TIPO_VEREDITO, carga, Enlace::VEREDITO_BYTES, quadro, sizeof(quadro));
  confere("monta quadro de veredito", n == (size_t)(Enlace::CABECALHO + 12 + 2));

  for (size_t i = 0; i < n; i++) rec.empurra(quadro[i]);
  bool veio = rec.proximo(q);
  Enlace::CargaVeredito v2;
  memset(&v2, 0, sizeof(v2));
  const bool iguais = veio && q.tipo == Enlace::TIPO_VEREDITO &&
                      Enlace::desserializa(q.carga, q.n, v2) &&
                      v2.probabilidade == v.probabilidade && v2.cobertura == v.cobertura &&
                      v2.exgMedio == v.exgMedio && v2.classe == v.classe && v2.ms == v.ms;
  confere("veredito volta identico byte a byte", iguais);

  // Lixo antes do quadro: e o log de boot da ROM da ESP32-CAM, que cai
  // no mesmo par de fios toda vez que ela reinicia.
  rec.reinicia();
  uint32_t s = 12345;
  for (int i = 0; i < 300; i++) {
    s = s * 1103515245u + 12345u;
    rec.empurra((uint8_t)(s >> 16));
  }
  for (size_t i = 0; i < n; i++) rec.empurra(quadro[i]);
  confere("acha o quadro depois de 300 bytes de lixo", rec.proximo(q));

  // Preambulo falso no meio do lixo: 0xA5 0x5A aparece por acaso a cada
  // 65 mil bytes, e sem recuperacao o receptor engoliria o quadro bom.
  rec.reinicia();
  rec.empurra(0xA5);
  rec.empurra(0x5A);
  rec.empurra(0x01);
  rec.empurra(0x11);
  rec.empurra(0x0C);  // promete 12 bytes que nunca vem inteiros
  for (int i = 0; i < 5; i++) rec.empurra(0xFF);
  for (size_t i = 0; i < n; i++) rec.empurra(quadro[i]);
  confere("recupera de preambulo falso", rec.proximo(q) && q.tipo == Enlace::TIPO_VEREDITO);

  // Um bit trocado tem de morrer no CRC, e nao virar decisao de irrigar.
  rec.reinicia();
  uint8_t sujo[Enlace::QUADRO_MAX];
  memcpy(sujo, quadro, n);
  sujo[7] ^= 0x08;
  for (size_t i = 0; i < n; i++) rec.empurra(sujo[i]);
  const bool passou = rec.proximo(q);
  confere("quadro com um bit trocado e recusado", !passou);

  // Dois quadros colados: um unico proximo() nao pode perder o segundo.
  rec.reinicia();
  for (size_t i = 0; i < n; i++) rec.empurra(quadro[i]);
  for (size_t i = 0; i < n; i++) rec.empurra(quadro[i]);
  const bool dois = rec.proximo(q) && rec.proximo(q) && !rec.proximo(q);
  confere("dois quadros colados saem os dois", dois);

  // Varredura: um bit trocado em qualquer posicao do quadro.
  int recusados = 0, total = 0;
  for (size_t i = 0; i < n; i++) {
    for (int b = 0; b < 8; b++) {
      Enlace::Receptor rr;
      memcpy(sujo, quadro, n);
      sujo[i] ^= (uint8_t)(1 << b);
      for (size_t k = 0; k < n; k++) rr.empurra(sujo[k]);
      Enlace::Quadro qq;
      total++;
      if (!rr.proximo(qq)) recusados++;
    }
  }
  Serial.printf("  varredura de bit unico: %d de %d recusados (%.1f%%)\n", recusados, total,
                100.0 * recusados / total);
  confere("nenhum quadro de um bit trocado passa", recusados == total);

  const Enlace::Contadores& c = rec.contadores();
  Serial.printf("  contadores: ok=%lu crc=%lu lixo=%lu\n", (unsigned long)c.quadrosOk,
                (unsigned long)c.crcErrado, (unsigned long)c.bytesDescartados);
}

// ---------------------------------------------------------------------
//  2. Banco de cenas -> caracteristicas
// ---------------------------------------------------------------------
static bool montaBanco() {
  Serial.println(F("\n--- 2. banco de cenas sinteticas ---"));

  uint8_t* quadro = (uint8_t*)malloc(Cenas::BYTES);
  if (!quadro) {
    Serial.println(F("  sem RAM para o quadro"));
    return false;
  }
  g_banco = (Amostra*)calloc(MAX_AMOSTRAS, sizeof(Amostra));
  if (!g_banco) {
    free(quadro);
    Serial.println(F("  sem RAM para o banco"));
    return false;
  }

  const Visao::Parametros par = Visao::Parametros::padrao();
  uint32_t somaUs             = 0;
  uint32_t quadros            = 0;
  g_n                         = 0;

  for (uint8_t t = 0; t < Cenas::N_TIPOS; t++) {
    const uint8_t rot = Cenas::rotulo(t);
    // Medias por tipo, so para poder olhar a tabela e entender o modelo.
    uint32_t soma[Visao::N_CARACTERISTICAS];
    memset(soma, 0, sizeof(soma));
    int usados = 0;

    for (int s = 0; s < SEMENTES_TOTAL; s++) {
      Cenas::desenha(t, (uint32_t)(t * 7919 + s * 104729 + 1), quadro);

      Visao::Caracteristicas c;
      const uint32_t t0 = micros();
      const bool ok     = Visao::extrai(quadro, Cenas::LARGURA, Cenas::ALTURA, 2, true, par, c);
      somaUs += micros() - t0;
      quadros++;
      if (!ok) continue;

      Amostra& a = g_banco[g_n];
      Visao::normaliza(c, a.x);
      a.tipo   = t;
      a.rotulo = rot;
      a.treino = (s < SEMENTES_TREINO);
      for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) soma[i] += (uint32_t)a.x[i];
      usados++;
      if (rot != 255) g_n++;  // cena escura entra na tabela, nao no treino
    }

    if (usados) {
      Serial.printf("  %-20s r=%-3d ", Cenas::nome(t), rot == 255 ? -1 : rot);
      for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) {
        Serial.printf("%4d ", (int)(soma[i] / usados));
      }
      Serial.println();
    }
  }

  g_usPorQuadro = quadros ? somaUs / quadros : 0;
  Serial.println(
      F("  colunas: cobert exgMed desvio bordas satur brilho maiorR clust perim calor (Q8)"));
  Serial.printf("  %d amostras avaliaveis, %lu us por quadro na extracao\n", g_n,
                (unsigned long)g_usPorQuadro);

  free(quadro);
  return g_n > 0;
}

// ---------------------------------------------------------------------
//  3. Treino: regressao logistica por descida de gradiente
//
//  Descida em lote, 600 epocas, com regularizacao L2. A L2 nao esta ai
//  por elegancia: sem ela os pesos crescem sem limite num conjunto quase
//  separavel, e peso grande nao cabe no int16 do Q8 - o modelo treinado
//  nao caberia no modelo executado.
//
//  RESTRICAO DE SINAL. O treino livre, rodado nesta placa em 09/09/2026,
//  chegou a 95,8% de acerto com peso NEGATIVO em exgMedio e em
//  perimetro. Ou seja: aprendeu que "verde demais e suspeito", porque no
//  banco sintetico os negativos dificeis (pano e plastico) sao os mais
//  verdes de todos. Dentro do banco isso e verdade e da acerto alto;
//  fora dele e falso, e derrubaria a primeira planta vicosa no sol.
//
//  A resposta nao e treinar mais - e proibir. Cada caracteristica ganha
//  um sinal permitido, vindo da fisica e nao dos dados, e a descida e
//  projetada de volta nesse semiespaco a cada passo. Isso custa alguns
//  pontos de acerto no banco e compra monotonicidade: mais verde nunca
//  pode DIMINUIR a chance de haver planta, mais saturado nunca pode
//  aumentar. Um modelo com essa garantia erra de forma previsivel; sem
//  ela, erra de forma criativa.
// ---------------------------------------------------------------------
static float g_w[Visao::N_CARACTERISTICAS];
static float g_b = 0.0f;

//  +1 exige peso >= 0    -1 exige peso <= 0    2 proibe a caracteristica
//
//  BRILHO ENTRA COMO PROIBIDO, e isso tambem saiu de um resultado medido
//  aqui: no treino livre ele recebeu peso -709, ou seja, o modelo
//  aprendeu "cena escura, provavelmente planta". No banco isso e
//  verdade - a unica cena escura positiva e folhagem na sombra, e as
//  claras (parede, ceu, solo seco) sao todas negativas - mas e um atalho
//  do gerador, nao um fato do mundo. Planta ao sol e clara. O indice ExG
//  ja e normalizado justamente para nao depender de intensidade; deixar
//  o brilho entrar na decisao desfaria essa propriedade. Ele continua
//  sendo medido, e continua servindo para a flag de luz baixa - que e
//  julgamento sobre a QUALIDADE do quadro, nao sobre o conteudo dele.
//
//  cobertura exgMedio desvio bordas satur brilho maiorR clusters perim calor
static const int8_t SINAL[Visao::N_CARACTERISTICAS] = {+1, +1, +1, +1, -1, 2, +1, +1, +1, +1};

static float sigmoide(float z) {
  if (z > 20.0f) return 1.0f;
  if (z < -20.0f) return 0.0f;
  return 1.0f / (1.0f + expf(-z));
}

static void treina(bool restrito) {
  Serial.printf("\n--- 3. treino da regressao logistica (%s) ---\n",
                restrito ? "com restricao de sinal" : "livre");

  for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) g_w[i] = 0.0f;
  g_b = 0.0f;

  const float taxa = 0.6f;
  const float l2   = 3e-4f;
  const int epocas = 600;
  int nTreino      = 0;
  for (int k = 0; k < g_n; k++)
    if (g_banco[k].treino) nTreino++;
  if (!nTreino) return;

  for (int e = 0; e < epocas; e++) {
    float gw[Visao::N_CARACTERISTICAS];
    float gb = 0.0f;
    for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) gw[i] = 0.0f;
    float perda = 0.0f;

    for (int k = 0; k < g_n; k++) {
      const Amostra& a = g_banco[k];
      if (!a.treino) continue;
      float z = g_b;
      for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) z += g_w[i] * (a.x[i] / 256.0f);
      const float p = sigmoide(z);
      const float y = (float)a.rotulo;
      const float d = p - y;
      for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) gw[i] += d * (a.x[i] / 256.0f);
      gb += d;
      perda -= y * logf(p + 1e-6f) + (1 - y) * logf(1 - p + 1e-6f);
    }

    for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) {
      g_w[i] -= taxa * (gw[i] / nTreino + l2 * g_w[i]);
      // Projecao: joga o peso de volta no semiespaco permitido. Uma
      // linha, e e ela que garante a monotonicidade do modelo inteiro.
      if (restrito) {
        if (SINAL[i] == 2) g_w[i] = 0.0f;
        if (SINAL[i] == 1 && g_w[i] < 0.0f) g_w[i] = 0.0f;
        if (SINAL[i] == -1 && g_w[i] > 0.0f) g_w[i] = 0.0f;
      }
    }
    g_b -= taxa * (gb / nTreino);

    if (e % 150 == 0 || e == epocas - 1) {
      Serial.printf("  epoca %3d  perda %.4f\n", e, perda / nTreino);
    }
  }
}

// ---------------------------------------------------------------------
//  4. Medida: acerto do modelo em ponto fixo, no conjunto reservado
// ---------------------------------------------------------------------
static int16_t emQ8(float v) {
  long q = lroundf(v * 256.0f);
  if (q > 32767) q = 32767;
  if (q < -32768) q = -32768;
  return (int16_t)q;
}

static void mede(const Visao::Pesos& p, const char* titulo) {
  Serial.printf("\n--- %s ---\n", titulo);

  int certoT = 0, totT = 0, certoV = 0, totV = 0;
  int falsoPos = 0, falsoNeg = 0;
  const Visao::Parametros par = Visao::Parametros::padrao();

  for (uint8_t t = 0; t < Cenas::N_TIPOS; t++) {
    int acertos = 0, total = 0;
    uint32_t somaProb = 0;
    for (int k = 0; k < g_n; k++) {
      const Amostra& a = g_banco[k];
      if (a.tipo != t) continue;

      int32_t acc = 0;
      for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) acc += (int32_t)p.w[i] * (int32_t)a.x[i];
      const uint16_t prob = Visao::sigmoidePermil((acc >> 8) + p.b);
      const bool disse    = prob >= par.limiarPlanta;
      const bool certo    = (disse == (a.rotulo == 1));

      somaProb += prob;
      total++;
      if (certo) acertos++;
      if (a.treino) {
        totT++;
        if (certo) certoT++;
      } else {
        totV++;
        if (certo) certoV++;
        if (!certo && a.rotulo == 0) falsoPos++;
        if (!certo && a.rotulo == 1) falsoNeg++;
      }
    }
    if (total) {
      Serial.printf("  %-20s %2d/%2d  prob media %4lu permil\n", Cenas::nome(t), acertos, total,
                    (unsigned long)(somaProb / total));
    }
  }

  Serial.printf("  treino  %d/%d  (%.1f%%)\n", certoT, totT, totT ? 100.0 * certoT / totT : 0.0);
  Serial.printf("  RESERVA %d/%d  (%.1f%%)  falso positivo %d  falso negativo %d\n", certoV, totV,
                totV ? 100.0 * certoV / totV : 0.0, falsoPos, falsoNeg);
}

static void imprimePesos(const Visao::Pesos& p) {
  Serial.println(F("\n--- pesos para colar em lib/farmio_visao/pesos.cpp ---"));
  Serial.print(F("const Pesos PESOS_PADRAO = {{"));
  for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) {
    Serial.printf("%d%s", p.w[i], i + 1 < Visao::N_CARACTERISTICAS ? ", " : "");
  }
  Serial.printf("}, %d};\n", p.b);
}

// ---------------------------------------------------------------------
static bool g_rodou = false;

static void roda() {
  Serial.println();
  Serial.println(F("====================================================="));
  Serial.printf("  FarmIO autoteste  |  %s %s\n", __DATE__, __TIME__);
  Serial.printf("  heap livre: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  Serial.println(F("====================================================="));

  testaEnlace();

  if (montaBanco()) {
    mede(Visao::PESOS_PADRAO, "4. acerto dos pesos ATUAIS (os que estao no firmware)");

    Visao::Pesos livre, restrito;
    treina(false);
    for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) livre.w[i] = emQ8(g_w[i]);
    livre.b = emQ8(g_b);
    mede(livre, "5. treino LIVRE, ja em ponto fixo Q8");

    treina(true);
    for (int i = 0; i < Visao::N_CARACTERISTICAS; i++) restrito.w[i] = emQ8(g_w[i]);
    restrito.b = emQ8(g_b);
    mede(restrito, "6. treino RESTRITO, ja em ponto fixo Q8");

    // O que vai para o firmware e o restrito: perder alguns pontos no
    // banco sintetico vale a garantia de que o modelo nao inverteu o
    // significado de "verde".
    Serial.println(F("\n=== livre (referencia, NAO usar) ==="));
    imprimePesos(livre);
    Serial.println(F("\n=== restrito (e este que vai para o firmware) ==="));
    imprimePesos(restrito);
  }

  Serial.printf("\n>>> %d falha(s) no enlace. Extracao: %lu us por quadro.\n", g_falhas,
                (unsigned long)g_usPorQuadro);
  Serial.println(F(">>> fim do autoteste"));
  g_rodou = true;
}

void setup() {
  Serial.begin(115200);
}

void loop() {
  // Nao roda antes de ter alguem lendo: com CDC nativo, tudo que sai
  // antes do monitor engatar se perde. Passados 10 s sem monitor, roda
  // assim mesmo - autoteste que exige plateia nao serve para CI.
  static const uint32_t limite = 10000;
  if (!g_rodou && (Serial || millis() > limite)) {
    delay(400);
    roda();
  }
  delay(50);
}
