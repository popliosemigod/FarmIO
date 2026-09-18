// =====================================================================
//  FarmIO - telemetria.h
//  O que cada componente esta lendo, escrito para pessoa ler no monitor
//  serial - nao para maquina consumir.
//
//  POR QUE ISTO EXISTE SEPARADO DO JSON. O firmware ja publicava
//  telemetria, mas em JSON de uma linha so, que e o formato certo para a
//  pagina web e o formato errado para quem esta com o multimetro na mao:
//  mil e cem caracteres numa linha, com o valor do solo entre o do
//  tanque e o da bomba, sem unidade e sem o pino de onde saiu. Dava para
//  ler, mas nao dava para bater o olho.
//
//  O bloco daqui responde tres coisas por componente, que sao as que a
//  bancada pergunta:
//
//      o numero cru .... porque e ele que calibra
//      o que ele virou . porque e isso que a logica decidiu
//      de onde ele veio  porque quando esta errado o suspeito e o fio
//
//  COMANDOS. O monitor serial e bidirecional e quase ninguem usa isso.
//  Uma letra digitada no monitor muda o que aparece, sem regravar a
//  placa - 'h' lista. A leitura e nao bloqueante, como tudo mais: se
//  ninguem digitar nada, nao custa nada.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>

#include "anel.h"
#include "config.h"
#include "energia.h"
#include "farmio_visao.h"
#include "sensores.h"
#include "web.h"

namespace Telemetria {

enum Modo : uint8_t {
  MODO_BLOCO = 0,  // o painel inteiro, a cada INTERVALO_SERIAL_MS
  MODO_LINHA,      // uma linha por vez, para acompanhar coisa que muda
  MODO_JSON,       // o mesmo do /sensores, para script consumir
  MODO_MUDO
};

inline uint8_t& modo() {
  static uint8_t m = MODO_BLOCO;
  return m;
}

inline uint32_t& intervalo() {
  static uint32_t v = INTERVALO_SERIAL_MS;
  return v;
}

// Barra de dez casas. Numero e barra juntos: o numero e para anotar, a
// barra e para perceber a tendencia sem ler numero nenhum.
inline void barra(uint8_t pct) {
  Serial.print('[');
  for (uint8_t i = 0; i < 10; i++) Serial.print(i * 10 < pct ? '#' : '.');
  Serial.print(']');
}

inline void ajuda() {
  Serial.println(F("\n  comandos do monitor:"));
  Serial.println(F("    h  esta ajuda"));
  Serial.println(F("    b  painel completo (padrao)"));
  Serial.println(F("    l  uma linha por leitura"));
  Serial.println(F("    j  JSON, o mesmo de /sensores"));
  Serial.println(F("    m  mudo"));
  Serial.println(F("    r  repete agora"));
  Serial.println(F("    z  zera os contadores de bomba e camera"));
  Serial.println(F("    p  pinagem e limiares em uso\n"));
}

inline void pinagem() {
  Serial.println(F("\n  ------ pinagem em uso ------"));
  Serial.printf("    DHT22 ......... GPIO%d   (pull-up de 10k para 3V3)\n", PIN_DHT);
  Serial.printf("    solo .......... GPIO%d   ADC1, alimentar em 3V3\n", PIN_SOLO);
  Serial.printf("    nivel ......... GPIO%d   ADC1, alimentar em 3V3\n", PIN_NIVEL);
  Serial.printf("    bomba (IN1) ... GPIO%d   %d Hz, pull-down de 10k\n", PIN_BOMBA_PWM,
                BOMBA_PWM_FREQ);
  Serial.printf("    anel .......... GPIO%d   %d pixels\n", PIN_ANEL, ANEL_PIXELS);
  Serial.printf("    OLED .......... SDA %d / SCL %d\n", PIN_SDA, PIN_SCL);
  Serial.printf("    camera ........ RX %d / TX %d / RST %d\n", PIN_CAM_RX, PIN_CAM_TX,
                PIN_CAM_RST);
  Serial.println(F("  ------ limiares ------"));
  Serial.printf("    solo: seco >=%d  baixo >=%d  alto <=%d  encharcado <=%d\n", SOLO_SECO_ADC,
                SOLO_BAIXO_ADC, SOLO_ALTO_ADC, SOLO_ENCHARCADO_ADC);
  Serial.printf("    tanque: vazio <=%d  cheio >=%d\n", NIVEL_VAZIO_ADC, NIVEL_CHEIO_ADC);
  Serial.printf("    bomba: pulso %d ms  descanso %d ms  teto %d ms\n", BOMBA_PASSO_MS,
                BOMBA_DESCANSO_MS, BOMBA_LIMITE_MS);
  Serial.printf("    driver: ate %d V, alvo de operacao %d V\n", BOMBA_DRIVER_VMAX_V,
                BOMBA_TENSAO_V);
  Serial.println(F("  NAO SAO MEDIDA: os limiares de solo e tanque sao chute"));
  Serial.println(F("  educado ate a calibracao. Ver docs/02.\n"));
}

// ---------------------------------------------------------------------
//  O painel
// ---------------------------------------------------------------------
inline void bloco() {
  Serial.println();
  Serial.println(F("====================================================================="));
  Serial.printf("  %s  %s      %lu s no ar      heap %lu B      RSSI %d dBm\n", FARMIO_NOME,
                FARMIO_VERSAO, (unsigned long)(millis() / 1000UL), (unsigned long)ESP.getFreeHeap(),
                WiFi.RSSI());
  Serial.println(F("====================================================================="));

  // ---- ar ----
  Serial.print(F("  AR        "));
  if (L.dhtOk && !isnan(L.temperaturaC)) {
    Serial.printf("%.1f C   umidade %.1f %%\n", L.temperaturaC, L.umidadeArPct);
  } else {
    Serial.println(F("--.- C   umidade --.- %   <<< SEM RESPOSTA"));
  }
  Serial.printf("            DHT22 no GPIO%d  |  %u falhas desde o boot\n", PIN_DHT, L.dhtFalhas);

  // ---- solo ----
  Serial.printf("\n  SOLO      ADC %4u de 4095   ->  %s\n", L.soloAdc,
                SOLO_NOME[L.soloFaixa <= SOLO_INVALIDO ? L.soloFaixa : SOLO_INVALIDO]);
  Serial.printf("            capacitivo no GPIO%d  |  seco >=%d  encharcado <=%d\n", PIN_SOLO,
                SOLO_SECO_ADC, SOLO_ENCHARCADO_ADC);

  // ---- tanque ----
  Serial.printf("\n  TANQUE    ADC %4u de 4095   ->  %3u %%   ", L.nivelAdc, L.tanquePct);
  barra(L.tanquePct);
  Serial.println();
  Serial.printf("            pente no GPIO%d  |  vazio <=%d  cheio >=%d\n", PIN_NIVEL,
                NIVEL_VAZIO_ADC, NIVEL_CHEIO_ADC);

  // ---- bomba ----
  Serial.printf("\n  BOMBA     %s", B.ligada ? "IRRIGANDO" : "parada   ");
  Serial.printf("          %u pulsos, %lu s no total\n", B.pulsos,
                (unsigned long)(B.tempoTotalMs / 1000UL));
  if (!B.ligada && B.bloqueioAtual && B.bloqueioAtual[0]) {
    Serial.printf("            bloqueio: %s\n", B.bloqueioAtual);
  }
  Serial.printf("            IN1 no GPIO%d, %d Hz  |  driver ate %d V, alvo %d V\n", PIN_BOMBA_PWM,
                BOMBA_PWM_FREQ, BOMBA_DRIVER_VMAX_V, BOMBA_TENSAO_V);

  // ---- camera ----
  Serial.print(F("\n  CAMERA    "));
  if (!V.enlaceOk) {
    Serial.println(F("SEM ENLACE"));
  } else if (V.temPlanta) {
    Serial.printf("PLANTA A VISTA    %u%% neste quadro, %u%% filtrado\n", V.probabilidade / 10,
                  V.mediaFiltrada / 10);
  } else {
    Serial.printf("sem planta        %u%% neste quadro, %u%% filtrado\n", V.probabilidade / 10,
                  V.mediaFiltrada / 10);
  }
  Serial.printf("            %u quadros, %u falhas, %u resets  |  UART1 RX=%d TX=%d\n", V.quadros,
                V.falhas, V.resets, PIN_CAM_RX, PIN_CAM_TX);
  if (V.enlaceOk) {
    Serial.printf("            verde na cena: %u%%   ultimo quadro levou %u ms\n", V.cobertura / 10,
                  V.msCamera);
  }

  // ---- energia ----
  const uint8_t brilho = Anel::brilhoAtual();
  const uint16_t ma    = Energia::estimativaMa(V.enlaceOk, brilho, B.ligada);
  const uint8_t usoPct = (uint8_t)((uint32_t)ma * 100UL / (uint32_t)Energia::teto());
  Serial.printf("\n  ENERGIA   %u mA de %u mA   ", ma, Energia::teto());
  barra(usoPct);
  Serial.printf("  %u%%\n", usoPct);
  Serial.printf("            fonte %s  |  anel em %u/255  |  ESTIMATIVA, nao medida\n",
                ENERGIA_FONTE_USB ? "USB" : "12 V", brilho);

  // ---- riscos ----
  Serial.print(F("\n  RISCOS    "));
  if (riscosAtivos == RISCO_NENHUM) {
    Serial.println(F("nenhum"));
  } else {
    bool primeiro = true;
    for (uint8_t i = 0; i < RISCO_QUANTOS; i++) {
      if (riscosAtivos & (1 << i)) {
        if (!primeiro) Serial.print(F(" | "));
        Serial.print(RISCO_TITULO[i]);
        primeiro = false;
      }
    }
    Serial.println();
  }

  // ---- rede ----
  Serial.print(F("\n  REDE      "));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(WiFi.localIP());
    Serial.printf("  em '%s'\n", WiFi.SSID().c_str());
  } else {
    Serial.printf("AP '%s' em ", FARMIO_NOME);
    Serial.println(WiFi.softAPIP());
  }
  if (V.ip[0]) Serial.printf("            video em http://%s:81/stream\n", V.ip);

  Serial.println(F("---------------------------------------------------------------------"));
  Serial.println(F("  'h' para os comandos"));
}

// Uma linha, para quando o interesse e ver um numero se mexer enquanto a
// mao mexe no sensor. O painel inteiro rolaria a tela e esconderia
// justamente a variacao.
inline void linha() {
  Serial.printf("%6lus  ", (unsigned long)(millis() / 1000UL));

  if (L.dhtOk && !isnan(L.temperaturaC)) {
    Serial.printf("T %5.1fC  UR %5.1f%%  ", L.temperaturaC, L.umidadeArPct);
  } else {
    Serial.print(F("T   --.-C  UR   --.-%  "));
  }

  Serial.printf("solo %4u %-13s  tanque %4u %3u%%  bomba %-9s  cam %s\n", L.soloAdc,
                SOLO_NOME[L.soloFaixa <= SOLO_INVALIDO ? L.soloFaixa : SOLO_INVALIDO], L.nivelAdc,
                L.tanquePct, B.ligada ? "IRRIGANDO" : "parada",
                !V.enlaceOk ? "--" : (V.temPlanta ? "planta" : "vazio"));
}

inline void json() {
  char buf[1100];
  Web::jsonSensores(buf, sizeof(buf));
  Serial.println(buf);
}

inline void imprimeAgora() {
  switch (modo()) {
    case MODO_BLOCO: bloco(); break;
    case MODO_LINHA: linha(); break;
    case MODO_JSON: json(); break;
    default: break;
  }
}

inline void cabecalhoDaLinha() {
  Serial.println(
      F("\n tempo   temperatura  umidade    solo                 tanque       bomba      camera"));
}

// Leitura nao bloqueante do que foi digitado no monitor.
inline void comandos() {
  while (Serial.available()) {
    const int c = Serial.read();
    switch (c) {
      case 'h':
      case '?': ajuda(); break;
      case 'b':
        modo() = MODO_BLOCO;
        bloco();
        break;
      case 'l':
        modo() = MODO_LINHA;
        cabecalhoDaLinha();
        break;
      case 'j':
        modo() = MODO_JSON;
        json();
        break;
      case 'm':
        modo() = MODO_MUDO;
        Serial.println(F("  mudo. 'b' para voltar."));
        break;
      case 'r': imprimeAgora(); break;
      case 'p': pinagem(); break;
      case 'z':
        B.pulsos       = 0;
        B.tempoTotalMs = 0;
        L.dhtFalhas    = 0;
        V.quadros = V.falhas = V.resets = 0;
        Serial.println(F("  contadores zerados."));
        break;
      default: break;  // \r, \n e o resto nao sao comando
    }
  }
}

inline void begin() {
  Serial.println(F("\n  telemetria: painel a cada 3 s. 'h' lista os comandos."));
}

// Chamar todo loop.
inline void tick() {
  comandos();

  static uint32_t proximo = 0;
  const uint32_t agora    = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + intervalo();

  imprimeAgora();
}

}  // namespace Telemetria
