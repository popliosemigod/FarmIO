// =====================================================================
//  FarmIO - tela.h
//  Display OLED SSD1306 128x64. Duas telas, como pede a especificacao:
//
//  TELA INICIAL          temperatura, umidade do ar, umidade do solo e
//                        porcentagem do tanque
//  TELA DE RISCO         interrompe brevemente para avisar, e volta
//
//  A tela de risco NAO fica no ar o tempo todo: ela aparece por
//  TELA_RISCO_MS e some, reaparecendo a cada TELA_RISCO_INTERVALO_MS
//  enquanto o risco durar. O motivo esta escrito na especificacao e vale
//  repetir: alerta permanente vira paisagem e para de ser lido.
// =====================================================================
#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "sensores.h"

namespace Tela {

inline Adafruit_SSD1306& oled() {
  static Adafruit_SSD1306 instancia(OLED_LARGURA, OLED_ALTURA, &Wire, -1);
  return instancia;
}

inline bool& presente() {
  static bool ok = false;
  return ok;
}

inline bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  // Modulo de OLED vem em 0x3C ou 0x3D conforme o lote. Tentar os dois
  // evita a manha de "o display nao liga" que na verdade e endereco.
  presente() =
      oled().begin(SSD1306_SWITCHCAPVCC, OLED_ADDR) || oled().begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (presente()) {
    oled().clearDisplay();
    oled().setTextColor(SSD1306_WHITE);
    oled().display();
  }
  return presente();
}

// Escreve um texto centralizado horizontalmente na linha y.
inline void centralizado(const char* txt, int16_t y, uint8_t tamanho) {
  int16_t x1, y1;
  uint16_t w, h;
  oled().setTextSize(tamanho);
  oled().getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  oled().setCursor((OLED_LARGURA - (int16_t)w) / 2, y);
  oled().print(txt);
}

inline void telaAbertura() {
  if (!presente()) return;
  oled().clearDisplay();
  centralizado("FarmIO", 16, 2);
  centralizado(FARMIO_VERSAO, 40, 1);
  oled().display();
}

inline void telaInicial() {
  Adafruit_SSD1306& d = oled();
  d.clearDisplay();
  d.setTextSize(1);

  char buf[26];

  // Temperatura em destaque: e a leitura que muda mais rapido e a que
  // dispara risco.
  if (L.dhtOk && !isnan(L.temperaturaC)) {
    snprintf(buf, sizeof(buf), "%.1f C", L.temperaturaC);
  } else {
    snprintf(buf, sizeof(buf), "-- C");
  }
  centralizado(buf, 0, 2);

  d.setTextSize(1);
  if (L.dhtOk && !isnan(L.umidadeArPct)) {
    snprintf(buf, sizeof(buf), "AR    %.0f%%", L.umidadeArPct);
  } else {
    snprintf(buf, sizeof(buf), "AR    --");
  }
  d.setCursor(4, 22);
  d.print(buf);

  d.setCursor(4, 34);
  d.print("SOLO  ");
  d.print(SOLO_NOME[L.soloFaixa <= SOLO_INVALIDO ? L.soloFaixa : SOLO_INVALIDO]);

  snprintf(buf, sizeof(buf), "TANQUE %u%%", L.tanquePct);
  d.setCursor(4, 46);
  d.print(buf);

  // Barra do tanque: ocupa a largura util e da a leitura de relance.
  const int16_t larguraBarra = OLED_LARGURA - 8;
  d.drawRect(4, 56, larguraBarra, 6, SSD1306_WHITE);
  d.fillRect(5, 57, (int16_t)((larguraBarra - 2) * L.tanquePct / 100), 4, SSD1306_WHITE);

  // Gota piscando quando a bomba esta irrigando de verdade.
  if (B.ligada) {
    d.fillCircle(OLED_LARGURA - 8, 26, 3, SSD1306_WHITE);
  }

  d.display();
}

inline void telaDeRisco(uint8_t riscos) {
  Adafruit_SSD1306& d = oled();
  d.clearDisplay();
  d.drawRect(0, 0, OLED_LARGURA, OLED_ALTURA, SSD1306_WHITE);
  centralizado("ATENCAO", 8, 2);
  centralizado(Sens::tituloDoRisco(riscos), 34, 1);

  // Diz o que fazer, nao so o que houve. Alerta sem acao vira ruido.
  const char* acao = "";
  if (riscos & RISCO_TANQUE_VAZIO)
    acao = "reabasteca o tanque";
  else if (riscos & RISCO_SOLO_ENCHARCADO)
    acao = "irrigacao bloqueada";
  else if (riscos & RISCO_SOLO_SECO)
    acao = "irrigando...";
  else if (riscos & RISCO_TEMPERATURA)
    acao = "verifique o sol";
  else if (riscos & RISCO_SENSOR_MUDO)
    acao = "cheque o DHT22";
  centralizado(acao, 48, 1);

  d.display();
}

// Chamar todo loop. Alterna entre a tela inicial e a de risco conforme o
// tempo, sem bloquear nada.
inline void tick(uint8_t riscos) {
  if (!presente()) return;

  static uint32_t proximoDesenho = 0;
  static uint32_t proximoAlerta  = 0;
  static uint32_t alertaAte      = 0;

  const uint32_t agora = millis();

  if (riscos != RISCO_NENHUM) {
    if (alertaAte == 0 && (int32_t)(agora - proximoAlerta) >= 0) {
      alertaAte     = agora + TELA_RISCO_MS;
      proximoAlerta = agora + TELA_RISCO_INTERVALO_MS;
    }
  } else {
    alertaAte     = 0;
    proximoAlerta = 0;  // risco resolvido: o proximo alerta e imediato
  }

  if (alertaAte && (int32_t)(agora - alertaAte) >= 0) alertaAte = 0;

  if ((int32_t)(agora - proximoDesenho) < 0) return;
  proximoDesenho = agora + INTERVALO_TELA_MS;

  if (alertaAte) {
    telaDeRisco(riscos);
  } else {
    telaInicial();
  }
}

}  // namespace Tela
