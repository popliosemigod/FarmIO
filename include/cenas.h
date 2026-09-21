// =====================================================================
//  FarmIO - cenas.h
//  Gerador de cenas sinteticas em RGB565, para treinar e medir o
//  classificador de planta SEM camera apontada para nada.
//
//  POR QUE CENA SINTETICA E NAO FOTO. Nao ha banco de fotos deste vaso:
//  a camera acabou de chegar na bancada e nao ha uma unica imagem
//  medida. Sem dado, so restariam duas saidas: chutar os pesos e chamar
//  de modelo, ou nao entregar modelo nenhum. A terceira saida e esta -
//  gerar cenas cujas ESTATISTICAS de cor e textura sao defensaveis a
//  partir do que se sabe de folha, terra e plastico, treinar em cima
//  delas e dizer com todas as letras o que isso vale.
//
//  O QUE ISSO VALE, EXATAMENTE. Acerto medido aqui NAO e acerto de
//  campo. Cena sintetica nao tem desfoque de lente, nem ruido de sensor
//  CMOS com pouca luz, nem o auto-ganho da OV2640 puxando a cor toda
//  para o cinza. O que este banco entrega e mais modesto e mais util:
//  garante que o modelo aprendeu a separar folhagem de superficie verde
//  lisa POR TEXTURA, e nao decorou "verde = planta" - que e o unico erro
//  que um classificador de nove caracteristicas pode cometer de forma
//  irrecuperavel. Os pesos que saem daqui sao ponto de partida honesto,
//  a ser recalibrado com foto real no primeiro ensaio com a camera.
//
//  As cenas dificeis (pano verde, vaso de plastico verde, muda pequena,
//  folhagem amarelada) existem justamente para forcar isso.
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Cenas {

// QQVGA. Com passo 2 cai exatamente na grade 80x60 do classificador, e
// 38,4 kB de quadro cabem na RAM do C3 com folga - o que permite rodar
// o treino inteiro na placa que ja esta na bancada.
static const int LARGURA  = 160;
static const int ALTURA   = 120;
static const size_t BYTES = (size_t)LARGURA * ALTURA * 2;

enum Tipo : uint8_t {
  FOLHAGEM_DENSA = 0,  // planta ocupando quase o quadro
  FOLHAGEM_ESPARSA,    // folhas sobre terra a mostra
  MUDA_PEQUENA,        // positivo dificil: pouca area verde
  FOLHAGEM_SOMBRA,     // positivo dificil: luz fraca, cor lavada
  FOLHAGEM_AMARELADA,  // positivo dificil: planta estressada
  SOLO_SECO,           // negativo
  SOLO_UMIDO,          // negativo
  MADEIRA,             // negativo: bancada
  PAREDE_BRANCA,       // negativo
  PANO_VERDE,          // negativo dificil: verde liso e saturado
  PLASTICO_VERDE,      // negativo dificil: vaso verde brilhante
  CEU,                 // negativo
  ESCURO,              // nao avaliavel: cena sem luz suficiente
  N_TIPOS
};

// 0 = sem planta, 1 = com planta, 255 = fora da avaliacao.
uint8_t rotulo(uint8_t tipo);
const char* nome(uint8_t tipo);

// Desenha a cena em 'quadro' (BYTES bytes, RGB565 byte alto primeiro,
// igual ao que a esp32-camera entrega). 'semente' varia a instancia:
// mesma semente, mesma imagem - o banco inteiro e reproduzivel.
void desenha(uint8_t tipo, uint32_t semente, uint8_t* quadro);

}  // namespace Cenas
