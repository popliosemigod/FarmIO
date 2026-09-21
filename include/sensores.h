// =====================================================================
//  FarmIO - sensores.h
//  Leitura do DHT22, da umidade do solo e do nivel do tanque.
//  Cada canal tem a propria cadencia: o DHT22 nao aceita mais de uma
//  leitura a cada 2 s, e o solo muda em minutos - ler tudo junto seria
//  desperdicio de energia no lento e perda de evento no rapido.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <DHT.h>

#include "config.h"

namespace Sens {

inline DHT& dht() {
  static DHT instancia(PIN_DHT, DHT22);
  return instancia;
}

// Mediana de 3: mata o pico isolado sem borrar a transicao real. E o
// filtro certo para ADC de solo, onde um outlier unico e ruido eletrico e
// uma mudanca sustentada e a planta secando.
struct Mediana3 {
  uint16_t a = 0, b = 0, c = 0;
  uint8_t n = 0;

  uint16_t push(uint16_t v) {
    a = b;
    b = c;
    c = v;
    if (n < 3) n++;
    if (n < 3) return v;
    const uint16_t mx = max(a, max(b, c));
    const uint16_t mn = min(a, min(b, c));
    return (uint16_t)(a + b + c - mx - mn);
  }
};

inline uint16_t leAdcMedio(uint8_t pino) {
  // O ADC do ESP32 e ruidoso: leitura unica varia dezenas de contagens
  // entre chamadas consecutivas. Oito amostras custam ~100 us e valem.
  uint32_t soma = 0;
  for (uint8_t i = 0; i < 8; i++) soma += analogRead(pino);
  return (uint16_t)(soma / 8);
}

// Leitura encostada num trilho do ADC nao e umidade: e sensor solto. Ver
// ADC_PISO_VALIDO em config.h - ate 21/09/2026 este teste nao existia, e
// o solo desconectado em 4095 era lido como "extremamente seco, irrigue".
inline bool adcPlausivel(uint16_t adc) {
  return adc > ADC_PISO_VALIDO && adc < ADC_TETO_VALIDO;
}

inline uint8_t faixaDoSolo(uint16_t adc) {
  if (!adcPlausivel(adc)) return SOLO_INVALIDO;
  if (adc >= SOLO_SECO_ADC) return SOLO_EXTREMAMENTE_BAIXA;
  if (adc >= SOLO_BAIXO_ADC) return SOLO_BAIXA;
  if (adc >= SOLO_ALTO_ADC) return SOLO_ESTAVEL;
  if (adc >= SOLO_ENCHARCADO_ADC) return SOLO_ALTA;
  return SOLO_EXTREMAMENTE_ALTA;
}

inline uint8_t porcentagemDoTanque(uint16_t adc) {
  if (adc <= NIVEL_VAZIO_ADC) return 0;
  if (adc >= NIVEL_CHEIO_ADC) return 100;
  const uint32_t faixa = NIVEL_CHEIO_ADC - NIVEL_VAZIO_ADC;
  return (uint8_t)(((uint32_t)(adc - NIVEL_VAZIO_ADC) * 100UL) / faixa);
}

inline void begin() {
  analogReadResolution(12);
  // 11 dB estende a faixa util do ADC ate ~3,1 V, que e onde os dois
  // sensores analogicos trabalham quando alimentados em 3V3.
  analogSetPinAttenuation(PIN_SOLO, ADC_11db);
  analogSetPinAttenuation(PIN_NIVEL, ADC_11db);
  dht().begin();

  L.temperaturaC = NAN;
  L.umidadeArPct = NAN;
  L.soloFaixa    = SOLO_INVALIDO;
  L.nivelValido  = false;  // ate a primeira leitura, nao se sabe: bomba parada
  L.dhtOk        = false;
}

// Chamar todo loop. Nao bloqueia: so le o canal cujo intervalo venceu.
inline void tick() {
  static uint32_t proxDht = 0, proxSolo = 0, proxNivel = 0;
  static Mediana3 filtroSolo, filtroNivel;
  const uint32_t agora = millis();

  if ((int32_t)(agora - proxSolo) >= 0) {
    proxSolo       = agora + INTERVALO_SOLO_MS;
    L.soloAdc      = filtroSolo.push(leAdcMedio(PIN_SOLO));
    L.soloFaixa    = faixaDoSolo(L.soloAdc);
    L.atualizadoEm = agora;
  }

  if ((int32_t)(agora - proxNivel) >= 0) {
    proxNivel   = agora + INTERVALO_NIVEL_MS;
    L.nivelAdc  = filtroNivel.push(leAdcMedio(PIN_NIVEL));
    L.tanquePct = porcentagemDoTanque(L.nivelAdc);
    // So o TETO invalida o nivel. Chao e tanque vazio de verdade, e tanque
    // vazio ja bloqueia a bomba - os dois defeitos caem no lado seguro.
    L.nivelValido  = L.nivelAdc < ADC_TETO_VALIDO;
    L.atualizadoEm = agora;
  }

  if ((int32_t)(agora - proxDht) >= 0) {
    proxDht       = agora + INTERVALO_DHT_MS;
    const float t = dht().readTemperature();
    const float h = dht().readHumidity();
    if (isnan(t) || isnan(h)) {
      // Nao apaga a ultima leitura boa: o DHT22 falha de vez em quando
      // e zerar a tela a cada falha isolada seria pior que manter o
      // ultimo valor valido e contar a falha.
      if (L.dhtFalhas < 0xFFFF) L.dhtFalhas++;
      if (L.dhtFalhas >= 4) L.dhtOk = false;
    } else {
      L.temperaturaC = t;
      L.umidadeArPct = h;
      L.dhtFalhas    = 0;
      L.dhtOk        = true;
    }
    L.atualizadoEm = agora;
  }
}

// Recalcula quais riscos estao ativos. Bitmask porque varios podem valer
// ao mesmo tempo - calor com tanque vazio e o caso ruim de verdade.
inline uint8_t avaliaRiscos() {
  uint8_t r = RISCO_NENHUM;

  if (L.dhtOk && !isnan(L.temperaturaC) && L.temperaturaC > TEMP_ALTA_C) {
    r |= RISCO_TEMPERATURA;
  }
  // Sensor sem resposta agora cobre os tres: DHT mudo, solo fora da faixa
  // fisica e nivel encostado no teto. Os tres pedem a mesma acao - olhar
  // o fio - e o painel da serial diz qual deles e.
  if (!L.dhtOk || L.soloFaixa == SOLO_INVALIDO || !L.nivelValido) r |= RISCO_SENSOR_MUDO;
  if (L.soloFaixa == SOLO_EXTREMAMENTE_BAIXA) r |= RISCO_SOLO_SECO;
  if (L.soloFaixa == SOLO_EXTREMAMENTE_ALTA) r |= RISCO_SOLO_ENCHARCADO;
  if (L.tanquePct == 0) r |= RISCO_TANQUE_VAZIO;

  return r;
}

// Nome do primeiro risco ativo, para a tela de alerta.
inline const char* tituloDoRisco(uint8_t riscos) {
  for (uint8_t i = 0; i < RISCO_QUANTOS; i++) {
    if (riscos & (1 << i)) return RISCO_TITULO[i];
  }
  return "";
}

}  // namespace Sens
