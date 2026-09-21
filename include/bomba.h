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
//
//  DOIS DONOS, UMA SAIDA. Desde 21/09/2026 a bomba tambem obedece a um
//  botao no app. O automatico e o manual sao camadas separadas:
//
//    - enquanto o manual esta ativo, o automatico nao roda - senao ele
//      desligaria a bomba no fim do pulso de 4 s dele;
//    - o manual tem contadores proprios, e o automatico nunca os le. O
//      pedido era que o botao nao influenciasse a logica do vaso, e esta
//      e a forma de garantir isso por construcao, e nao por cuidado.
//
//  O QUE O BOTAO NAO PASSA POR CIMA. Os intertravamentos se dividem em
//  dois grupos, e o criterio e quem consegue ver o problema a tempo:
//
//    hardware - energia, tanque vazio, tanque sem leitura. Valem SEMPRE.
//               Bomba girando a seco queima em minutos, e nenhuma pessoa
//               olhando o vaso ve o fundo do tanque a tempo.
//    planta   - solo encharcado, solo sem leitura, teto do ciclo. Valem
//               so no AUTOMATICO. No manual, quem decide se a planta
//               precisa de agua e a pessoa que esta olhando para ela.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"
#include "energia.h"

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

// So o hardware. Nao toca em contador nenhum - quem chama decide o que
// contabilizar, e e isso que deixa o manual fora das contas do automatico.
inline void acionaSaida(bool ligar) {
#ifndef BOMBA_PINO_UNICO
  digitalWrite(PIN_BOMBA_STBY, ligar ? HIGH : LOW);
  digitalWrite(PIN_BOMBA_IN1, ligar ? HIGH : LOW);
  digitalWrite(PIN_BOMBA_IN2, LOW);
#endif
  B.duty = ligar ? BOMBA_PWM_MAX : 0;
  pwmWrite(B.duty);
}

// ---------------------------------------------------------------------
//  Automatico: o ciclo de pulsos
// ---------------------------------------------------------------------
inline void desliga(const char* motivo) {
  if (B.ligada) {
    B.tempoTotalMs += millis() - B.ligadaDesde;
    B.ligada = false;
  }
  acionaSaida(false);
  B.bloqueioAtual = motivo;
}

inline void liga() {
  if (!B.ligada) {
    B.ligada      = true;
    B.ligadaDesde = millis();
    B.pulsos++;
  }
  acionaSaida(true);
  B.bloqueioAtual = "";
}

inline void begin() {
#ifndef BOMBA_PINO_UNICO
  pinMode(PIN_BOMBA_IN1, OUTPUT);
  pinMode(PIN_BOMBA_IN2, OUTPUT);
  pinMode(PIN_BOMBA_STBY, OUTPUT);
#endif
  pwmSetup();
  desliga("boot");
}

// Protecao do HARDWARE: vale para o automatico e para o botao do app.
// Devolve o motivo, ou nullptr se a bomba pode girar.
inline const char* protecaoDoHardware() {
  // Energia vem antes de tudo porque e o unico bloqueio que nao adianta
  // esperar passar.
  const char* energia = Energia::bombaBloqueadaPorEnergia(V.enlaceOk);
  if (energia) return energia;

  if (!L.nivelValido) return "sem leitura de nivel";
  if (L.tanquePct == 0) return "tanque vazio";
  return nullptr;
}

// Todos os bloqueios do automatico: hardware + planta.
//
// A ORDEM IMPORTA: o primeiro motivo encontrado e o que aparece na tela,
// e ele tem de ser o mais fundamental.
inline const char* motivoDeBloqueio() {
  const char* hw = protecaoDoHardware();
  if (hw) return hw;

  if (L.soloFaixa == SOLO_EXTREMAMENTE_ALTA) return "solo encharcado";
  if (L.soloFaixa == SOLO_INVALIDO) return "sem leitura de solo";
  if (B.tempoTotalMs > BOMBA_LIMITE_MS) return "limite de irrigacao do ciclo";

#if BOMBA_EXIGE_PLANTA
  // Desligado por padrao, e a razao esta em config.h: camera suja ou as
  // escuras viraria "nao ha planta", e a planta secaria por causa de uma
  // lente empoeirada. So ligar isto depois de medir o falso negativo com
  // planta de verdade.
  if (V.enlaceOk && !V.temPlanta) return "nenhuma planta a vista";
#endif

  return nullptr;
}

// ---------------------------------------------------------------------
//  Manual: o botao do app
// ---------------------------------------------------------------------
inline void desligaManual(const char* motivo) {
  if (!B.manual) return;
  B.manualTotalMs += millis() - B.manualDesde;
  B.manual = false;
  B.ligada = false;
  acionaSaida(false);
  B.bloqueioAtual = motivo;
  Serial.printf("[bomba] manual encerrado: %s\n", motivo);
}

// Pedido de ligar vindo do app. Devolve nullptr se ligou (ou ja estava
// ligada e o prazo foi renovado), ou o motivo da recusa.
inline const char* ligaManual() {
  const char* hw = protecaoDoHardware();
  if (hw) return hw;

  const uint32_t agora = millis();
  if (!B.manual) {
    // O automatico estava no meio de um pulso: encerra pelo caminho normal
    // dele, com a contabilidade e o descanso que ele mesmo faria.
    if (B.ligada) {
      B.ultimoPulso = agora;
      desliga("pulso encerrado pelo app");
    }
    B.manual      = true;
    B.manualDesde = agora;
    B.manualAte   = agora + BOMBA_MANUAL_MAX_MS;
    B.manualAcionamentos++;
    B.ligada = true;  // estado FISICO: energia, tela e anel precisam saber
    acionaSaida(true);
    B.bloqueioAtual = "";
    Serial.println("[bomba] manual: ligada pelo app");
  }
  B.manualRenovadoEm = agora;
  return nullptr;
}

// A pagina renova a cada 2 s. Renovar nao estende o teto absoluto.
inline bool renovaManual() {
  if (!B.manual) return false;
  B.manualRenovadoEm = millis();
  return true;
}

inline uint16_t manualRestanteS() {
  if (!B.manual) return 0;
  const int32_t r = (int32_t)(B.manualAte - millis());
  return r > 0 ? (uint16_t)((r + 999) / 1000) : 0;
}

inline void tickManual() {
  const uint32_t agora = millis();

  const char* hw = protecaoDoHardware();
  if (hw) {
    desligaManual(hw);
    return;
  }
  if ((int32_t)(agora - B.manualAte) >= 0) {
    desligaManual("manual: tempo maximo atingido");
    return;
  }
  if (agora - B.manualRenovadoEm > BOMBA_MANUAL_LEASE_MS) {
    desligaManual("manual: o app parou de responder");
    return;
  }
}

// ---------------------------------------------------------------------
//  Chamar todo loop. Toda a decisao de irrigar mora aqui.
// ---------------------------------------------------------------------
inline void tick() {
  if (B.manual) {
    tickManual();
    return;  // automatico suspenso enquanto o app estiver no comando
  }

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
