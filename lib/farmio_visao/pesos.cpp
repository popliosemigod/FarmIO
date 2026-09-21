// =====================================================================
//  FarmIO - pesos.cpp
//  Os onze numeros que sao o modelo: dez pesos e um vies, em Q8.
//
//  ESTE ARQUIVO E GERADO, NAO ESCRITO A MAO. Ele sai do ambiente
//  `autoteste`, que treina a regressao na propria placa em cima do
//  banco de cenas sinteticas de src/cenas.cpp e imprime o bloco abaixo
//  pronto para colar. O procedimento inteiro esta em
//  docs/05-visao-planta.md.
//
//  Mora sozinho num arquivo porque retreinar tem de mexer em um lugar
//  so - e para que o diff de um retreino seja legivel: onze numeros.
// =====================================================================
#include "farmio_visao.h"

namespace Visao {

// Ordem: cobertura, exgMedio, exgDesvio, bordas, saturacao, brilho,
//        maiorRegiao, clusters, perimetro, calor.
//
//  Treino de 09/09/2026 no ESP32-C3 da bancada, com restricao de sinal e
//  as dez caracteristicas: 97,9% no conjunto reservado, zero falso
//  negativo, dois falsos positivos. O caminho ate aqui - e os tres
//  modelos descartados - esta em docs/05-visao-planta.md e no diario.
//
//  Ler estes numeros diz o que o modelo aprendeu:
//    bordas 4,55 e desvio 3,06 ....... textura domina a decisao
//    calor 2,71 ...................... vermelho acima de azul e vegetacao
//    clusters 1,73 ................... folhagem vem em varios pedacos
//    exgMedio 0,38 e cobertura 0,55 .. cor entra, mas pouco
//    saturacao, brilho, perimetro .... zerados: nao carregam informacao
//
//  Cor pesando pouco NAO e defeito: e o resultado que se queria. Um
//  modelo em que a cor decidisse chamaria o balde verde de planta.
const Pesos PESOS_PADRAO = {{141, 96, 784, 1165, 0, 0, 0, 442, 0, 693}, -1085};

}  // namespace Visao
