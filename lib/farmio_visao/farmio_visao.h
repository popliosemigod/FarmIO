// =====================================================================
//  FarmIO - farmio_visao.h
//  "Tem planta na frente da camera?" - o classificador mais simples que
//  responde essa pergunta com honestidade.
//
//  O QUE ESTE ARQUIVO NAO E. Nao e rede neural, nao e TensorFlow Lite e
//  nao reconhece especie. E um extrator de nove caracteristicas seguido
//  de uma regressao logistica de nove pesos. Cabe em poucos kB, roda em
//  dezenas de milissegundos e - o que mais importa - cada numero dele
//  pode ser explicado. Quando ele erra, da para ver qual caracteristica
//  levou ao erro; numa rede convolucional de 200 kB nao daria.
//
//  POR QUE NAO BASTA "CONTAR PIXEL VERDE". Um pano verde, um vaso de
//  plastico verde e uma parede pintada de verde tem MAIS verde puro que
//  uma folha de verdade - folha reflete pouco e tem sombra propria. A
//  contagem de verde sozinha classifica o balde de plastico como planta
//  e a muda pequena como fundo. O que separa folhagem de superficie
//  verde lisa nao e a cor, e a IRREGULARIDADE: folha tem nervura, borda
//  recortada, sombra entre camadas e brilho especular em ponto. Por isso
//  das nove caracteristicas, cinco medem textura e forma e so quatro
//  medem cor.
//
//  INDICE USADO. ExG normalizado (excesso de verde):
//
//      ExG = 255 * (2G - R - B) / (R + G + B)
//
//  A divisao pela soma e o detalhe que faz o indice funcionar de manha e
//  de tarde: ela cancela a intensidade da luz e deixa so a cromaticidade.
//  Sem normalizar, a mesma folha na sombra cai para metade do valor e o
//  limiar fixo perde a planta justamente quando o vaso esta na sombra.
//
//  LIMIAR DE OTSU. O corte entre "verde" e "fundo" nao e constante: sai
//  do histograma de cada quadro, por Otsu, que escolhe o valor que
//  maximiza a separacao entre as duas populacoes. Um piso fixo continua
//  existindo, porque numa cena inteira marrom o Otsu ainda parte o
//  histograma no meio e chamaria terra de folha.
//
//  C++11 puro, sem Arduino e sem alocacao dinamica: o autoteste roda
//  este arquivo inteiro na placa, e um dia rodara no PC.
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Visao {

// Grade de amostragem. Uma QVGA 320x240 com passo 4, ou uma QQVGA
// 160x120 com passo 2, caem exatamente nesta grade de 80x60.
// Por que subamostrar: 4800 amostras ja estabilizam as estatisticas, e
// custam 16x menos que os 76800 pixels da QVGA inteira.
static const int GRADE_LARG = 80;
static const int GRADE_ALT  = 60;
static const int GRADE_N    = GRADE_LARG * GRADE_ALT;

// Grade grossa de blocos, para medir forma sem custo de rotulagem
// pixel a pixel. Cada bloco cobre 8x8 amostras.
static const int BLOCO_LARG = 10;
static const int BLOCO_ALT  = 8;
static const int BLOCO_N    = BLOCO_LARG * BLOCO_ALT;

static const int N_CARACTERISTICAS = 10;

// Classes devolvidas ao vaso.
enum Classe : uint8_t {
  SEM_PLANTA = 0,
  PROVAVEL   = 1,
  PLANTA     = 2
};

// ---------------------------------------------------------------------
//  Parametros ajustaveis. Ficam aqui com um valor padrao defensavel, e
//  o firmware sobrescreve a partir de include/config.h - que continua
//  sendo o unico lugar do projeto onde se mexe em limiar.
// ---------------------------------------------------------------------
struct Parametros {
  uint8_t pisoExg;        // ExG minimo para um pixel poder ser "verde"
  uint8_t brilhoMinimo;   // abaixo disso a cena e escura demais para opinar
  uint8_t brilhoMaximo;   // acima disso o quadro esta estourado
  uint8_t blocoVerdePct;  // % de amostras verdes para o bloco contar como verde
  uint16_t limiarPlanta;  // permil: acima disso e PLANTA
  uint16_t limiarDuvida;  // permil: acima disso e PROVAVEL

  static Parametros padrao();
};

// ---------------------------------------------------------------------
//  As nove caracteristicas, em unidades inteiras legiveis. Elas vao
//  inteiras para a serial no modo bancada: caracteristica que nao da
//  para ler nao da para depurar.
// ---------------------------------------------------------------------
struct Caracteristicas {
  uint16_t cobertura;    // permil de amostras acima do limiar
  uint8_t exgMedio;      // ExG medio DENTRO da mascara
  uint8_t exgDesvio;     // desvio do ExG RELATIVO a media - textura de folha
  uint8_t bordas;        // gradiente do ExG RELATIVO a media - recorte
  uint8_t saturacao;     // saturacao media na mascara
  uint8_t brilho;        // brilho medio da cena inteira
  uint16_t maiorRegiao;  // permil da cena no maior aglomerado conexo
  uint8_t clusters;      // quantos aglomerados separados
  uint16_t perimetro;    // perimetro/area da mascara, em permil - recorte
  uint8_t calor;         // equilibrio R x B na mascara: 128 e neutro
  uint8_t limiarUsado;   // limiar de Otsu efetivamente aplicado
  uint8_t flags;         // FLAG_* do enlace: luz baixa, estourado
};

struct Veredito {
  uint16_t probabilidade;  // permil
  uint8_t classe;          // Classe
  Caracteristicas c;
};

// ---------------------------------------------------------------------
//  Pesos da regressao logistica, em ponto fixo Q8 (256 = 1,0).
//
//  Ponto fixo e nao float por dois motivos: o ESP32-C3 nao tem unidade
//  de ponto flutuante - float nele e biblioteca, dezenas de ciclos por
//  operacao - e porque inteiro da o MESMO resultado no PC, na CAM e no
//  C3, o que permite comparar veredito entre plataformas byte a byte.
// ---------------------------------------------------------------------
struct Pesos {
  int16_t w[N_CARACTERISTICAS];
  int16_t b;
};

// Pesos ajustados pelo autoteste; ver docs/05-visao-planta.md.
extern const Pesos PESOS_PADRAO;

// Normaliza as caracteristicas para Q8 (256 = 1,0), na mesma ordem e com
// as mesmas escalas usadas no treino. Publica porque o autoteste treina
// em cima dela.
void normaliza(const Caracteristicas& c, int16_t x[N_CARACTERISTICAS]);

// Sigmoide em permil, com z em Q8. Determinista e sem float.
uint16_t sigmoidePermil(int32_t zQ8);

uint16_t classifica(const Caracteristicas& c, const Pesos& p);

// ---------------------------------------------------------------------
//  Extracao. 'quadro' e RGB565; 'trocaBytes' vale para o formato que a
//  esp32-camera entrega (byte alto primeiro).
//  'passo' e o fator de subamostragem: larg/passo tem de caber em
//  GRADE_LARG e alt/passo em GRADE_ALT.
// ---------------------------------------------------------------------
bool extrai(const uint8_t* quadro, int larg, int alt, int passo, bool trocaBytes,
            const Parametros& par, Caracteristicas& fora);

// Extracao + classificacao + rotulo.
bool avalia(const uint8_t* quadro, int larg, int alt, int passo, bool trocaBytes,
            const Parametros& par, const Pesos& pesos, Veredito& fora);

// ---------------------------------------------------------------------
//  Filtro temporal, usado no lado do VASO.
//
//  Um quadro isolado nao decide nada: passar a mao na frente da camera,
//  uma nuvem ou o auto-ganho da camera fazem a probabilidade pular. O
//  vaso so muda de opiniao quando a maioria dos ultimos N vereditos
//  concorda, e usa histerese para nao ficar oscilando na fronteira.
// ---------------------------------------------------------------------
class Filtro {
public:
  Filtro();

  void reinicia();
  void empurra(uint16_t probabilidadePermil);

  bool temPlanta() const { return temPlanta_; }
  uint16_t media() const;
  uint8_t amostras() const { return usadas_; }

private:
  static const uint8_t JANELA = 8;
  uint16_t hist_[JANELA];
  uint8_t pos_;
  uint8_t usadas_;
  bool temPlanta_;
};

}  // namespace Visao
