/*
 * schedule.h
 * -----------------------------------------------------------------------
 * Modulo de programacion horaria y logica de control:
 *   - Decide si toca filtrar segun el programa (hora inicio/fin + dias).
 *   - Aplica la logica de modo AUTO (calienta con el circuito solar
 *     cuando conviene, con histeresis para no hacer parpadear las
 *     valvulas).
 *   - Gestiona el bloqueo temporal de comandos mientras las valvulas
 *     estan fisicamente girando (VALVE_MOVE_MS).
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Debe llamarse una vez en setup(), despues de storageLoadSchedule().
void setupSchedule();

// Debe llamarse en cada vuelta del loop() principal. Aplica la logica de
// programa horario + modo automatico, y libera el bloqueo de valvulas
// cuando corresponda.
void loopSchedule();

// Cambia el estado deseado de las valvulas de forma segura: si hay un
// cambio real, aplica el rele y activa el bloqueo temporal de VALVE_MOVE_MS.
// Reutilizable desde el modulo web o desde la logica automatica.
void setValves(bool activo);

// Activa/desactiva el modo automatico. En ON, el programa horario puede
// controlar bomba y valvulas; en OFF, el programa no actua aunque este
// dentro de su horario.
void setAutoEnabled(bool enabled);

// Fuerza la bomba encendida/apagada a mano. Se suma (OR) al estado que
// decida el modo automatico: sirve para tener bomba cuando no hay
// programa corriendo.
void setPumpManual(bool on);

// Selector manual de valvulas (mutuamente excluyente: true=SOLAR,
// false=FILTRO). Solo tiene efecto inmediato en las valvulas cuando el
// modo automatico no esta mandando en ese momento.
void setForceSolar(bool solar);

// true si el momento actual (segun RTC del ESP32) cae dentro del programa
// de filtracion configurado
bool scheduleIsActiveNow();

// Calcula el proximo inicio de programa. Devuelve epoch (segundos) del
// proximo inicio, o 0 si no hay ningun dia configurado.
time_t scheduleNextStart();
