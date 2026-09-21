// =====================================================================
//  FarmIO - energia.h
//  O orcamento de corrente do vaso, tratado como restricao de projeto e
//  nao como esperanca.
//
//  O PEDIDO ERA "O PROJETO INTEIRO FUNCIONANDO NA ALIMENTACAO USB". Isso
//  tem uma parte que da e uma parte que nao da, e o firmware precisa
//  saber qual e qual - porque a parte que nao da, se for tentada, nao
//  falha com mensagem: reinicia a placa por subtensao no meio da
//  irrigacao, e o vaso volta achando que nunca irrigou.
//
//  O QUE CABE numa porta USB 2.0 de 500 mA:
//
//      ESP32-C3 com Wi-Fi sem sleep ......  95 mA
//      ESP32-CAM capturando QVGA .........  180 mA
//      OLED SSD1306 ......................  20 mA
//      DHT22 .............................  2 mA
//      ------------------------------------------
//      soma ..............................  297 mA
//      margem de 20% para picos de radio .  100 mA
//      ------------------------------------------
//      sobra para o anel de LED ..........  118 mA
//
//  Cento e dezoito miliamperes dao brilho 31 de 255 no anel de 16
//  pixels - abaixo dos 40 que a especificacao do SmartFarm pedia. O
//  firmware NAO ignora esse teto: ele calcula o brilho a partir do
//  orcamento, toda vez. Trocar a porta por um carregador de 1,5 A faz o
//  anel voltar aos 40 sem recompilar nada.
//
//  O QUE NAO CABE: a bomba RS-385. Ela e de 12 V, e uma porta USB
//  entrega 5 V - nao ha o que negociar em tensao. Se houvesse um
//  elevador de 5 V para 12 V, ele puxaria da USB algo como
//  12 V x 1,5 A / 5 V / 0,85 = 4,2 A, oito vezes o que a porta promete.
//  Portanto: em ENERGIA_FONTE_USB, a bomba fica bloqueada por projeto, e
//  o bloqueio aparece na tela e no JSON com o motivo escrito. Bloqueio
//  silencioso e o pior tipo - ver docs/03-logica-de-operacao.md.
//
//  A saida honesta para irrigar em USB e trocar a bomba por uma de
//  diafragma de 5 V (~350 mA), que cabe: 282 + 350 = 632 mA excede os
//  500 mA da USB 2.0, mas cabe numa porta de 900 mA ou num carregador -
//  e cabe com o anel apagado durante o pulso, que e o que este arquivo
//  faz. As contas inteiras estao em docs/06-energia-usb.md.
//
//  TODO NUMERO AQUI E DE DATASHEET, NAO DE AMPERIMETRO. Enquanto nao
//  houver medida na bancada, o orcamento erra de proposito para o lado
//  conservador: errar para menos apaga LED, errar para mais reinicia a
//  placa.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace Energia {

// A bomba so entra na conta da porta USB se ela beber dessa porta. Com
// fonte propria (BOMBA_FONTE_SEPARADA), a corrente dela passa por outro
// fio e nao aparece aqui - nem para bloquear, nem para descontar do anel.
static const bool BOMBA_NA_USB = (BOMBA_EM_5V != 0) && (BOMBA_FONTE_SEPARADA == 0);

// O teto pode CAIR sozinho em operacao. Se a placa reiniciar por
// subtensao, a hipotese mais provavel e que a porta entrega menos do que
// diz - cabo fino, hub barato, porta de teclado. O vaso desce um degrau,
// grava a decisao e segue funcionando com menos LED, em vez de entrar em
// laco de reinicio. E a unica adaptacao que ele consegue fazer sozinho.
inline uint16_t& teto() {
  static uint16_t v = ENERGIA_TETO_MA;
  return v;
}

inline uint16_t& quedas() {
  static uint16_t v = 0;
  return v;
}

inline Preferences& memoria() {
  static Preferences p;
  return p;
}

inline void begin() {
  memoria().begin("farmio", false);
  quedas() = memoria().getUShort("brownouts", 0);

  const esp_reset_reason_t motivo = esp_reset_reason();
  if (motivo == ESP_RST_BROWNOUT) {
    if (quedas() < 1000) quedas()++;
    memoria().putUShort("brownouts", quedas());
    Serial.printf("[energia] reinicio por subtensao (%u no total)\n", quedas());
  }

  // Cada queda por subtensao tira 15% do teto, ate o piso de 250 mA.
  // Abaixo disso nem o C3 com a camera cabem, e o problema deixa de ser
  // orcamento e passa a ser cabo ou fonte - caso para a bancada, nao
  // para o firmware.
  uint32_t t = ENERGIA_TETO_MA;
  for (uint16_t i = 0; i < quedas() && t > 250; i++) t = (t * 85) / 100;
  teto() = (uint16_t)(t < 250 ? 250 : t);

  Serial.printf("[energia] fonte %s, teto %u mA, margem %d%%\n", ENERGIA_FONTE_USB ? "USB" : "12 V",
                teto(), ENERGIA_MARGEM_PCT);
}

inline uint16_t margemMa() {
  return (uint16_t)(((uint32_t)teto() * ENERGIA_MARGEM_PCT) / 100);
}

// Consumo que existe sempre, esteja o vaso fazendo o que estiver.
inline uint16_t baseMa(bool cameraViva) {
  uint16_t ma = ENERGIA_C3_MA + ENERGIA_OLED_MA + ENERGIA_DHT_MA;
  if (cameraViva) ma += ENERGIA_CAM_MA;
  return ma;
}

// Corrente maxima do anel: 16 pixels x 60 mA, escalada pelo brilho.
inline uint16_t anelMa(uint8_t brilho) {
  return (uint16_t)(((uint32_t)ANEL_PIXELS * ENERGIA_PIXEL_MA_CHEIO * brilho) / 255);
}

// O teto de brilho que sobra depois de pagar tudo o mais. E ele, e nao
// ANEL_BRILHO, que o anel obedece.
inline uint8_t brilhoPermitido(bool cameraViva, bool bombaLigada) {
  int32_t sobra = (int32_t)teto() - (int32_t)margemMa() - (int32_t)baseMa(cameraViva);
  if (bombaLigada && BOMBA_NA_USB) sobra -= ENERGIA_BOMBA_MA;
  if (sobra <= 0) return 0;

  const uint32_t cheio = (uint32_t)ANEL_PIXELS * ENERGIA_PIXEL_MA_CHEIO;
  uint32_t b           = ((uint32_t)sobra * 255) / cheio;
  if (b > ANEL_BRILHO) b = ANEL_BRILHO;  // conforto visual continua mandando
  return (uint8_t)b;
}

// A bomba pode ligar do ponto de vista de ENERGIA? Devolve nullptr se
// sim, ou o motivo do bloqueio em texto. O motivo vai para a tela e para
// o JSON: bloqueio sem explicacao parece defeito.
inline const char* bombaBloqueadaPorEnergia(bool cameraViva) {
#if BOMBA_FONTE_SEPARADA
  // Fonte propria de 7 a 9 V: a porta USB nao alimenta a bomba, entao nao
  // ha o que o orcamento dela bloquear.
  (void)cameraViva;
  return nullptr;
#elif ENERGIA_FONTE_USB
#if !BOMBA_EM_5V
  // Bomba de 12 V numa fonte de 5 V. Nao e questao de corrente.
  return "bomba de 12 V nao roda em USB";
#else
  const int32_t sobra = (int32_t)teto() - (int32_t)margemMa() - (int32_t)baseMa(cameraViva);
  if (sobra < ENERGIA_BOMBA_MA) return "corrente insuficiente na USB";
  return nullptr;
#endif
#else
  (void)cameraViva;
  return nullptr;  // fonte de 12 V: a bomba e o motivo de ela existir
#endif
}

// Pedir um quadro a camera e o segundo maior pico do conjunto. Enquanto
// a bomba estiver girando, a captura espera - dez segundos de atraso na
// deteccao de planta nao custam nada, e dois picos somados custam um
// reinicio por subtensao.
inline bool podeCapturar(bool bombaLigada) {
  // Com a bomba em fonte propria os dois picos nao se somam mais na mesma
  // porta, e a restricao perde a razao de existir.
  if (BOMBA_FONTE_SEPARADA) return true;
  return !bombaLigada;
}

// Estimativa do consumo atual, para a telemetria. Quem tem amperimetro
// na bancada compara este numero com o medido - e a divergencia entre os
// dois e o que corrige a tabela do config.h.
inline uint16_t estimativaMa(bool cameraViva, uint8_t brilho, bool bombaLigada) {
  uint16_t ma = baseMa(cameraViva) + anelMa(brilho);
  if (bombaLigada && BOMBA_NA_USB) ma += ENERGIA_BOMBA_MA;
  return ma;
}

}  // namespace Energia
