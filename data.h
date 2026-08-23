/*
 * data.h
 * -----------------------------------------------------------------------
 * Estructuras de datos compartidas por todos los modulos.
 * Se usan structs simples (sin clases) para mantener el codigo legible
 * y facilmente reutilizable entre proyectos.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Un tramo horario de programa de filtracion/calentamiento
struct ScheduleProgram {
  uint8_t startHour   = 8;
  uint8_t startMinute = 0;
  uint8_t endHour     = 10;
  uint8_t endMinute   = 0;
  bool days[7]        = {false,true,true,true,true,true,false}; // 0=Domingo .. 6=Sabado
};

// Estado global del sistema (temperaturas, actuadores, programa, etc.)
struct SystemState {
  float tJacuzzi   = 0.0f;   // Temperatura del agua del jacuzzi (T1), ya con offset aplicado
  float tSolar     = 0.0f;   // Temperatura del serpentin solar (T2), ya con offset aplicado
  float offsetT1   = 0.0f;   // Offset de calibracion sumado a la lectura cruda de T1
  float offsetT2   = 0.0f;   // Offset de calibracion sumado a la lectura cruda de T2
  bool  pumpOn     = false;  // Estado real (resultante) de la bomba
  bool v1open      = false;  // false = recto a filtro | true = desvia a serpentin
  bool v2open      = false;  // false = cerrada        | true = abierta (retorno solar)

  // --- Interruptores independientes de control (nueva logica de botones) ---
  bool autoEnabled = false;  // ON = el programa horario puede mandar; OFF = el programa no actua aunque este en horario
  bool pumpManual  = false;  // Fuerza la bomba encendida a mano (util cuando no hay programa activo)
  bool forceSolar  = false;  // Selector manual de valvulas cuando no manda el auto: true=SOLAR, false=FILTRO

  float targetTemp = 36.0f;  // Temperatura deseada por el usuario
  float solarDischargeTemp = 60.0f; // Limite de temperatura del serpentin solar (T2), solo informativo/guardado
  ScheduleProgram schedule;  // Programa de filtracion configurado

  bool valvesLocked = false; // true mientras las valvulas estan girando (bloquea comandos)
  unsigned long valveLockUntil = 0; // millis() en el que se libera el bloqueo
};

// Instancia unica del estado, definida en el .ino y accesible con "extern"
// desde cualquier modulo que incluya este archivo.
extern SystemState g_state;
