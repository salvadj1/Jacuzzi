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
#include "storage.h"
#include <time.h>

void setupSchedule() {
  g_state.valvesLocked = false;
  g_state.valveLockUntil = 0;
}

void setValves(bool activo) {
  bool changed = (activo != g_state.valvulasActivas);
  g_state.valvulasActivas = activo;

  relayValves(activo);

  if (changed) {
    g_state.valvesLocked = true;
    g_state.valveLockUntil = millis() + VALVE_MOVE_MS;
    Serial.printf("[SCHED] Valvulas cambiando: %s (bloqueadas %lus)\n",
      activo ? "DESVIO SOLAR" : "REPOSO/FILTRO", VALVE_MOVE_MS/1000);
  }
}

void setAutoEnabled(bool enabled) {
  g_state.autoEnabled = enabled;
  storageSaveAutoEnabled(); // persistido: si el ESP32 se reinicia, vuelve con el mismo estado
  Serial.printf("[SCHED] Modo auto %s\n", enabled ? "ACTIVADO" : "DESACTIVADO");
  // Si se desactiva el auto, las valvulas pasan a obedecer inmediatamente
  // al selector manual (forceSolar); lo decide loopSchedule() en el
  // siguiente ciclo, aqui no forzamos nada mas para no pelear con el bloqueo.
}

void setPumpManual(bool on) {
  g_state.pumpManual = on;
  Serial.printf("[SCHED] Bomba manual %s\n", on ? "ACTIVADA" : "DESACTIVADA");
}

void setForceSolar(bool solar) {
  g_state.forceSolar = solar;
  Serial.printf("[SCHED] Forzado manual de valvulas: %s\n", solar ? "SOLAR" : "FILTRO");
  // Solo aplicamos ya mismo si el auto no esta mandando en este instante;
  // si el auto esta activo y en horario, autoHeatingLogic() decide y este
  // selector queda memorizado para cuando el auto deje de mandar.
  if (!(g_state.autoEnabled && scheduleIsActiveNow())) {
    setValves(solar);
  }
}

bool scheduleIsActiveNow() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) return false; // hora aun no sincronizada

  int nowMinutes   = timeInfo.tm_hour * 60 + timeInfo.tm_min;
  int startMinutes = g_state.schedule.startHour * 60 + g_state.schedule.startMinute;
  int endMinutes   = g_state.schedule.endHour * 60 + g_state.schedule.endMinute;

  if (startMinutes <= endMinutes) {
    // Franja normal, dentro del mismo dia (ej. 08:00 - 18:00)
    if (!g_state.schedule.days[timeInfo.tm_wday]) return false;
    return nowMinutes >= startMinutes && nowMinutes < endMinutes;
  }

  // Franja que cruza medianoche (ej. 22:39 - 18:00 del dia siguiente):
  // esta activa desde el inicio hasta medianoche, y desde medianoche
  // hasta el fin. El dia marcado corresponde al dia de INICIO.
  bool afterMidnightPart = nowMinutes < endMinutes;
  int  wdayToCheck = afterMidnightPart
    ? (timeInfo.tm_wday + 6) % 7   // venimos del dia anterior (el de inicio)
    : timeInfo.tm_wday;
  if (!g_state.schedule.days[wdayToCheck]) return false;

  return nowMinutes >= startMinutes || afterMidnightPart;
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

// Logica de descarga del serpentin solar: mientras T1 no alcance la
// temperatura objetivo, se fuerza solar en ventanas de 5 min cada vez que
// T2 supera el umbral de descarga. Pasados los 5 min se da por descargado
// el serpentin y se vuelve a filtro, reevaluando T2 en el siguiente ciclo.
static void autoHeatingLogic() {
  if (g_state.valvesLocked) return;

  // Temperatura ya alcanzada: no hace falta descargar, valvulas a filtro.
  if (g_state.tJacuzzi >= g_state.targetTemp) {
    g_state.dischargeActive = false;
    setValves(false);
    return;
  }

  if (g_state.dischargeActive) {
    // Descarga en curso: se mantiene hasta cumplir los 5 min.
    if (millis() >= g_state.dischargeUntil) {
      g_state.dischargeActive = false;
      Serial.println("[SCHED] Descarga de serpentin completada (5 min)");
      setValves(false);
    }
    // Si aun no ha pasado el tiempo, se deja tal cual (valvulas ya abiertas).
    return;
  }

  // No hay descarga en curso: se inicia solo si T2 supera el umbral.
  if (g_state.tSolar > g_state.solarDischargeTemp) {
    g_state.dischargeActive  = true;
    g_state.dischargeUntil   = millis() + DISCHARGE_DURATION_MS;
    Serial.println("[SCHED] Iniciando descarga de serpentin (5 min)");
    setValves(true);
  } else {
    setValves(false);
  }
}

void loopSchedule() {
  // Libera el bloqueo de valvulas cuando ha pasado el tiempo de giro
  if (g_state.valvesLocked && millis() > g_state.valveLockUntil) {
    g_state.valvesLocked = false;
    Serial.println("[SCHED] Valvulas listas, bloqueo liberado");
  }

  // El auto solo manda si esta activado Y estamos dentro de su horario
  bool autoActiveNow = g_state.autoEnabled && scheduleIsActiveNow();

  // La bomba real es la suma de "manual" y "auto mandando ahora mismo"
  bool pumpShouldRun = g_state.pumpManual || autoActiveNow;
  if (pumpShouldRun != g_state.pumpOn) {
    Serial.printf("[SCHED] Bomba %s (manual=%s, auto=%s)\n",
      pumpShouldRun ? "ENCENDIDA" : "APAGADA",
      g_state.pumpManual ? "ON" : "OFF", autoActiveNow ? "ON" : "OFF");
  }
  g_state.pumpOn = pumpShouldRun;
  relayPump(pumpShouldRun);

  if (g_state.valvesLocked) return;

  if (autoActiveNow) {
    // El auto manda: decide valvulas por temperatura (con histeresis)
    autoHeatingLogic();
  } else {
    // El auto no manda (desactivado o fuera de horario): las valvulas
    // obedecen al selector manual forceSolar/forceFilter
    setValves(g_state.forceSolar);
  }
}