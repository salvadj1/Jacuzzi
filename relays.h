/*
 * relays.h
 * -----------------------------------------------------------------------
 * Modulo de control del modulo de 4 reles (bomba, rele de ambas valvulas
 * y un rele de repuesto). Funciones sueltas y reutilizables: pueden
 * copiarse a cualquier otro proyecto que use un modulo de reles similar.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Configura los pines de los reles como salida y los deja apagados.
// Llamar una vez en setup().
void setupRelays();

// Enciende/apaga la bomba de circulacion. Tiene en cuenta la polaridad
// configurada en RELAY_ACTIVE_LOW.
void relayPump(bool on);

// Activa/desactiva el rele que mueve las dos valvulas a la vez. Al estar
// una en Normalmente Abierta y la otra en Normalmente Cerrada, un unico
// rele basta para dejarlas siempre en posiciones opuestas:
//   false = reposo (filtro directo)
//   true  = activado (desvio al serpentin solar)
void relayValves(bool activo);

// Activa/desactiva el rele de repuesto (uso futuro)
void relaySpare(bool on);
