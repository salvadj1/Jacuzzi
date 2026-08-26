/*
 * storage.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de almacenamiento persistente (NVS).
 * -----------------------------------------------------------------------
 */
#include "storage.h"
#include <Preferences.h>

// Espacios de nombres (namespaces) separados dentro de la NVS para no
// mezclar claves de distintos modulos.
static Preferences prefsWifi;
static Preferences prefsSchedule;
static Preferences prefsTemp;

void storageInit() {
  // Solo se abren/cierran los "namespaces" en cada operacion para evitar
  // dejar la NVS abierta todo el tiempo; aqui solo comprobamos que exista.
  prefsWifi.begin("wifi", false);
  prefsWifi.end();
  prefsSchedule.begin("schedule", false);
  prefsSchedule.end();
  prefsTemp.begin("temp", false);
  prefsTemp.end();
}

// ---------------- Redes WiFi conocidas ----------------
// Se guardan como claves "ssid0"/"pass0" .. "ssid4"/"pass4" dentro del
// namespace "wifi", mas un contador "count".

int storageGetKnownNetworksCount() {
  prefsWifi.begin("wifi", true);
  int count = prefsWifi.getInt("count", 0);
  prefsWifi.end();
  return count;
}

KnownNetwork storageGetKnownNetwork(int index) {
  KnownNetwork net;
  prefsWifi.begin("wifi", true);
  net.ssid     = prefsWifi.getString(("ssid" + String(index)).c_str(), "");
  net.password = prefsWifi.getString(("pass" + String(index)).c_str(), "");
  prefsWifi.end();
  return net;
}

void storageSaveKnownNetwork(const String &ssid, const String &password) {
  prefsWifi.begin("wifi", false);
  int count = prefsWifi.getInt("count", 0);

  // Si el SSID ya estaba guardado, solo actualizamos su password
  for (int i = 0; i < count; i++) {
    String existing = prefsWifi.getString(("ssid" + String(i)).c_str(), "");
    if (existing == ssid) {
      prefsWifi.putString(("pass" + String(i)).c_str(), password);
      prefsWifi.end();
      return;
    }
  }

  // Si no existia, la añadimos al final (o sobrescribimos la mas antigua
  // si ya se alcanzo el maximo de redes guardadas)
  int slot = (count < MAX_KNOWN_NETWORKS) ? count : 0;
  prefsWifi.putString(("ssid" + String(slot)).c_str(), ssid);
  prefsWifi.putString(("pass" + String(slot)).c_str(), password);
  if (count < MAX_KNOWN_NETWORKS) {
    prefsWifi.putInt("count", count + 1);
  }
  prefsWifi.end();
}

void storageDeleteKnownNetwork(int index) {
  prefsWifi.begin("wifi", false);
  int count = prefsWifi.getInt("count", 0);
  if (index < 0 || index >= count) { prefsWifi.end(); return; }

  // Desplaza todas las redes posteriores una posicion hacia atras
  for (int i = index; i < count - 1; i++) {
    String ssid = prefsWifi.getString(("ssid" + String(i + 1)).c_str(), "");
    String pass = prefsWifi.getString(("pass" + String(i + 1)).c_str(), "");
    prefsWifi.putString(("ssid" + String(i)).c_str(), ssid);
    prefsWifi.putString(("pass" + String(i)).c_str(), pass);
  }
  prefsWifi.remove(("ssid" + String(count - 1)).c_str());
  prefsWifi.remove(("pass" + String(count - 1)).c_str());
  prefsWifi.putInt("count", count - 1);
  prefsWifi.end();
}

void storageMoveKnownNetwork(int index, int delta) {
  prefsWifi.begin("wifi", false);
  int count = prefsWifi.getInt("count", 0);
  int other = index + delta;
  if (index < 0 || index >= count || other < 0 || other >= count) { prefsWifi.end(); return; }

  String ssidA = prefsWifi.getString(("ssid" + String(index)).c_str(), "");
  String passA = prefsWifi.getString(("pass" + String(index)).c_str(), "");
  String ssidB = prefsWifi.getString(("ssid" + String(other)).c_str(), "");
  String passB = prefsWifi.getString(("pass" + String(other)).c_str(), "");

  prefsWifi.putString(("ssid" + String(index)).c_str(), ssidB);
  prefsWifi.putString(("pass" + String(index)).c_str(), passB);
  prefsWifi.putString(("ssid" + String(other)).c_str(), ssidA);
  prefsWifi.putString(("pass" + String(other)).c_str(), passA);
  prefsWifi.end();
}

void storageClearKnownNetworks() {
  prefsWifi.begin("wifi", false);
  prefsWifi.clear();
  prefsWifi.end();
}

// ---------------- Modo de conexion WiFi ----------------
void storageLoadWifiMode(int &mode) {
  prefsWifi.begin("wifi", true);
  mode = prefsWifi.getInt("mode", 0); // 0 = redes disponibles, 1 = AP permanente
  prefsWifi.end();
}

void storageSaveWifiMode(int mode) {
  prefsWifi.begin("wifi", false);
  prefsWifi.putInt("mode", mode);
  prefsWifi.end();
}

// ---------------- Programa de filtracion ----------------
void storageLoadSchedule() {
  prefsSchedule.begin("schedule", true);
  g_state.schedule.startHour   = prefsSchedule.getUChar("sh", 8);
  g_state.schedule.startMinute = prefsSchedule.getUChar("sm", 0);
  g_state.schedule.endHour     = prefsSchedule.getUChar("eh", 10);
  g_state.schedule.endMinute   = prefsSchedule.getUChar("em", 0);
  for (int i = 0; i < 7; i++) {
    g_state.schedule.days[i] = prefsSchedule.getBool(("d" + String(i)).c_str(), (i >= 1 && i <= 5));
  }
  prefsSchedule.end();
}

void storageSaveSchedule() {
  prefsSchedule.begin("schedule", false);
  prefsSchedule.putUChar("sh", g_state.schedule.startHour);
  prefsSchedule.putUChar("sm", g_state.schedule.startMinute);
  prefsSchedule.putUChar("eh", g_state.schedule.endHour);
  prefsSchedule.putUChar("em", g_state.schedule.endMinute);
  for (int i = 0; i < 7; i++) {
    prefsSchedule.putBool(("d" + String(i)).c_str(), g_state.schedule.days[i]);
  }
  prefsSchedule.end();
}

// ---------------- Temperatura objetivo ----------------
void storageLoadTargetTemp() {
  prefsTemp.begin("temp", true);
  g_state.targetTemp = prefsTemp.getFloat("target", 36.0f);
  prefsTemp.end();
}

void storageSaveTargetTemp() {
  prefsTemp.begin("temp", false);
  prefsTemp.putFloat("target", g_state.targetTemp);
  prefsTemp.end();
}

// ---------------- Offset de calibracion de sensores ----------------
// Reutiliza el mismo namespace "temp" que la temperatura objetivo.
void storageLoadTempOffsets() {
  prefsTemp.begin("temp", true);
  g_state.offsetT1 = prefsTemp.getFloat("offT1", 0.0f);
  g_state.offsetT2 = prefsTemp.getFloat("offT2", 0.0f);
  prefsTemp.end();
}

void storageSaveTempOffsets() {
  prefsTemp.begin("temp", false);
  prefsTemp.putFloat("offT1", g_state.offsetT1);
  prefsTemp.putFloat("offT2", g_state.offsetT2);
  prefsTemp.end();
}

// ---------------- Estado del modo automatico ----------------
// Reutiliza el namespace "schedule" (tiene relacion directa con el programa).
void storageLoadAutoEnabled() {
  prefsSchedule.begin("schedule", true);
  g_state.autoEnabled = prefsSchedule.getBool("autoOn", false);
  prefsSchedule.end();
}

void storageSaveAutoEnabled() {
  prefsSchedule.begin("schedule", false);
  prefsSchedule.putBool("autoOn", g_state.autoEnabled);
  prefsSchedule.end();
}

// ---------------- Limite de descarga solar ----------------
// Reutiliza el mismo namespace "temp".
void storageLoadSolarDischargeTemp() {
  prefsTemp.begin("temp", true);
  g_state.solarDischargeTemp = prefsTemp.getFloat("solarDis", 60.0f);
  prefsTemp.end();
}

void storageSaveSolarDischargeTemp() {
  prefsTemp.begin("temp", false);
  prefsTemp.putFloat("solarDis", g_state.solarDischargeTemp);
  prefsTemp.end();
}
