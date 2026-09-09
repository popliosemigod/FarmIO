// =====================================================================
//  FarmIO - bomba.h
//  Acionamento da irrigacao, com os intertravamentos que impedem o vaso
//  de afogar a planta ou queimar a bomba.
//
//  A regra vem da especificacao do SmartFarm e vale mais que qualquer
//  otimizacao: a bomba SO liga com solo extremamente seco, e NUNCA liga
//  com tanque vazio ou solo encharcado. Bomba de diafragma girando a
//  seco se danifica em minutos.
//
//  A irrigacao e pulsada de proposito. A agua leva dezenas de segundos
//  para percolar do dreno ate o sensor; irrigar em malha fechada continua
//  faria a bomba despejar o tanque inteiro antes de a leitura reagir.
//  Pulso curto, espera longa, mede de novo.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"

namespace Bomba {

// Compatibilidade LEDC entre o core Arduino-ESP32 2.x e 3.x.
inline void pwmSetup() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BOMBA_PWM, BOMBA_PWM_FREQ, BOMBA_PWM_BITS);
#else
  ledcSetup(BOMBA_CANAL_LEDC, BOMBA_PWM_FREQ, BOMBA_PWM_BITS);
  ledcAttachPin(PIN_BOMBA_PWM, BOMBA_CANAL_LEDC);
#endif
}

inline void pwmWrite(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_BOMBA_PWM, duty);
#else
  ledcWrite(BOMBA_CANAL_LEDC, duty);
#endif
}

inline void desliga(const char* motivo) {
  if (B.ligada) {
    B.tempoTotalMs += millis() - B.ligadaDesde;
    B.ligada = false;
  }
  B.duty = 0;
  pwmWrite(0);
  digitalWrite(PIN_BOMBA_IN1, LOW);
  digitalWrite(PIN_BOMBA_IN2, LOW);
  digitalWrite(PIN_BOMBA_STBY, LOW);
  B.bloqueioAtual = motivo;
}

inline void liga() {
  if (!B.ligada) {
    B.ligada      = true;
    B.ligadaDesde = millis();
    B.pulsos++;
  }
  digitalWrite(PIN_BOMBA_STBY, HIGH);
  digitalWrite(PIN_BOMBA_IN1, HIGH);
  digitalWrite(PIN_BOMBA_IN2, LOW);
  B.duty = BOMBA_PWM_MAX;
  pwmWrite(B.duty);
  B.bloqueioAtual = "";
}

inline void begin() {
  pinMode(PIN_BOMBA_IN1, OUTPUT);
  pinMode(PIN_BOMBA_IN2, OUTPUT);
  pinMode(PIN_BOMBA_STBY, OUTPUT);
  pwmSetup();
  desliga("boot");
}

// Devolve o motivo do bloqueio, ou nullptr se pode irrigar.
inline const char* motivoDeBloqueio() {
  if (L.tanquePct == 0) return "tanque vazio";
  if (L.soloFaixa == SOLO_EXTREMAMENTE_ALTA) return "solo encharcado";
  if (L.soloFaixa == SOLO_INVALIDO) return "sem leitura de solo";
  if (B.tempoTotalMs > BOMBA_LIMITE_MS) return "limite de irrigacao do ciclo";
  return nullptr;
}

// Chamar todo loop. Toda a decisao de irrigar mora aqui.
inline void tick() {
  const uint32_t agora = millis();
  const char* bloqueio = motivoDeBloqueio();

  // Intertravamento tem prioridade absoluta sobre qualquer pedido de
  // irrigacao: desliga na hora, mesmo no meio de um pulso.
  if (bloqueio) {
    desliga(bloqueio);
    return;
  }

  if (B.ligada) {
    const bool fimDoPulso = (agora - B.ligadaDesde) >= BOMBA_PASSO_MS;
    const bool jaMolhou   = (L.soloFaixa != SOLO_EXTREMAMENTE_BAIXA);
    if (fimDoPulso || jaMolhou) {
      B.ultimoPulso = agora;
      desliga(jaMolhou ? "solo saiu do vermelho" : "descansando entre pulsos");
    }
    return;
  }

  // Desligada: so volta a ligar se o solo ainda pede agua e o descanso
  // ja passou.
  if (L.soloFaixa != SOLO_EXTREMAMENTE_BAIXA) {
    B.bloqueioAtual = "solo nao pede agua";
    return;
  }
  if (B.ultimoPulso && (agora - B.ultimoPulso) < BOMBA_DESCANSO_MS) {
    B.bloqueioAtual = "descansando entre pulsos";
    return;
  }
  liga();
}

}  // namespace Bomba
