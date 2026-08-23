/*
 * schedule.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de programacion horaria y logica de control.
 * Requiere que la hora del sistema este sincronizada por NTP (se hace en
 * el .ino principal con configTime(), una vez hay conexion WiFi).
 * -----------------------------------------------------------------------
 */
#include "schedule.h"
#include "config.h"
#include "data.h"
#include "relays.h"
#include <time.h>

void setupSchedule() {
  g_state.valvesLocked = false;
  g_state.valveLockUntil = 0;
}

void setValves(bool v1open, bool v2open) {
  bool changed = (v1open != g_state.v1open) || (v2open != g_state.v2open);
  g_state.v1open = v1open;
  g_state.v2open = v2open;

  relayValve1(v1open);
  relayValve2(v2open);

  if (changed) {
    g_state.valvesLocked = true;
    g_state.valveLockUntil = millis() + VALVE_MOVE_MS;
    Serial.printf("[SCHED] Valvulas cambiando: V1=%s V2=%s (bloqueadas %lus)\n",
      v1open ? "ABIERTA" : "CERRADA", v2open ? "ABIERTA" : "CERRADA", VALVE_MOVE_MS/1000);
  }
}

void setSystemMode(int mode) {
  g_state.mode = (SystemMode)mode;
  const char* nombres[] = {"BYPASS", "SOLAR", "AUTO"};
  Serial.printf("[SCHED] Modo cambiado a %s\n", nombres[g_state.mode]);
  if (g_state.mode == MODE_SOLAR) {
    setValves(true, true);
  } else if (g_state.mode == MODE_BYPASS) {
    setValves(false, false);
  }
  // En modo AUTO no forzamos nada aqui: lo decide loopSchedule()/autoLogic()
}

bool scheduleIsActiveNow() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) return false; // hora aun no sincronizada

  if (!g_state.schedule.days[timeInfo.tm_wday]) return false;

  int nowMinutes   = timeInfo.tm_hour * 60 + timeInfo.tm_min;
  int startMinutes = g_state.schedule.startHour * 60 + g_state.schedule.startMinute;
  int endMinutes   = g_state.schedule.endHour * 60 + g_state.schedule.endMinute;

  return nowMinutes >= startMinutes && nowMinutes < endMinutes;
}

time_t scheduleNextStart() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) return 0;

  time_t now = mktime(&timeInfo);
  for (int addDays = 0; addDays < 8; addDays++) {
    struct tm candidate = timeInfo;
    candidate.tm_mday += addDays;
    candidate.tm_hour = g_state.schedule.startHour;
    candidate.tm_min  = g_state.schedule.startMinute;
    candidate.tm_sec  = 0;
    time_t candidateTime = mktime(&candidate); // normaliza tm_wday tras sumar dias

    struct tm normalized;
    localtime_r(&candidateTime, &normalized);
    if (g_state.schedule.days[normalized.tm_wday] && candidateTime > now) {
      return candidateTime;
    }
  }
  return 0; // ningun dia configurado en el programa
}

// Logica de calentamiento automatico con histeresis: se activa con un
// margen amplio y se desactiva con un margen estrecho, para evitar que
// pequeñas oscilaciones del sensor hagan cambiar las valvulas sin parar.
static void autoHeatingLogic() {
  if (g_state.mode != MODE_AUTO) return;
  if (g_state.valvesLocked) return;

  bool shouldHeat = g_state.v1open
    ? (g_state.tSolar > g_state.tJacuzzi + 0.5f && g_state.tJacuzzi < g_state.targetTemp)
    : (g_state.tSolar > g_state.tJacuzzi + 3.0f && g_state.tJacuzzi < g_state.targetTemp);

  setValves(shouldHeat, shouldHeat);
}

void loopSchedule() {
  // Libera el bloqueo de valvulas cuando ha pasado el tiempo de giro
  if (g_state.valvesLocked && millis() > g_state.valveLockUntil) {
    g_state.valvesLocked = false;
    Serial.println("[SCHED] Valvulas listas, bloqueo liberado");
  }

  // La bomba solo funciona dentro del horario programado
  bool activeNow = scheduleIsActiveNow();
  if (activeNow != g_state.pumpOn) {
    Serial.printf("[SCHED] Bomba %s (%s de horario programado)\n",
      activeNow ? "ENCENDIDA" : "APAGADA", activeNow ? "dentro" : "fuera");
  }
  g_state.pumpOn = activeNow;
  relayPump(activeNow);

  if (!activeNow && !g_state.valvesLocked) {
    // Fuera de horario: valvulas en reposo (recto a filtro, sin retorno solar)
    if (g_state.v1open || g_state.v2open) setValves(false, false);
    return;
  }

  autoHeatingLogic();
}
