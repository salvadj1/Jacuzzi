/*
 * relays.h
 * -----------------------------------------------------------------------
 * Modulo de control del modulo de 4 reles (bomba, valvula V1, valvula V2
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

// Abre/cierra la valvula V1 (false = recto a filtro, true = desvia a
// serpentin solar). Internamente activa el rele el tiempo justo o lo deja
// activo segun el tipo de valvula (ver comentario en relays.cpp).
void relayValve1(bool open);

// Abre/cierra la valvula V2 (retorno del circuito solar)
void relayValve2(bool open);

// Activa/desactiva el rele de repuesto (uso futuro)
void relaySpare(bool on);
