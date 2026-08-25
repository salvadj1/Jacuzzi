/*
 * relays.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de reles.
 *
 * NOTA sobre las valvulas: se asume que son valvulas motorizadas de 2
 * hilos controladas por UN solo rele cada una (rele activado = valvula en
 * posicion "abierta/desviada", rele desactivado = valvula en posicion de
 * reposo). Si tus valvulas necesitan un rele para abrir y otro para
 * cerrar, basta con añadir un segundo pin y replicar el mismo patron.
 * -----------------------------------------------------------------------
 */
#include "relays.h"
#include "config.h"

// Traduce un estado logico (encendido/abierto = true) al nivel electrico
// real segun la polaridad configurada del modulo de reles.
static inline void writeRelay(uint8_t pin, bool logicalOn) {
  bool level = RELAY_ACTIVE_LOW ? !logicalOn : logicalOn;
  digitalWrite(pin, level ? HIGH : LOW);
}

void setupRelays() {
  pinMode(PIN_RELAY_MOTOR, OUTPUT);
  pinMode(PIN_DE_AMBAS_VALVULAS, OUTPUT);
  pinMode(PIN_RELAY_SPARE, OUTPUT);

  // Todos apagados al arrancar, por seguridad
  writeRelay(PIN_RELAY_MOTOR, false);
  writeRelay(PIN_DE_AMBAS_VALVULAS, false);
  writeRelay(PIN_RELAY_SPARE, false);

  Serial.println("[RELAYS] Reles inicializados (todos apagados)");
}

void relayPump(bool on) {
  writeRelay(PIN_RELAY_MOTOR, on);
}

void relayValves(bool activo) {
  writeRelay(PIN_DE_AMBAS_VALVULAS, activo);
}

void relaySpare(bool on) {
  writeRelay(PIN_RELAY_SPARE, on);
}
