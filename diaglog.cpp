/*
 * diaglog.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de registro de diagnostico. Ver diaglog.h
 * para el diseño general (buffer circular en RAM + respaldo en NVS por
 * trozos, igual patron que datalog.cpp).
 * -----------------------------------------------------------------------
 */
#include "diaglog.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>
#include <string.h>

// Namespace propio en NVS para no mezclar con datalog.cpp ni storage.cpp
static Preferences prefsDiag;

#define DIAG_CHUNK_ENTRIES 50
#define DIAG_NUM_CHUNKS (DIAG_LOG_CAPACITY_ENTRIES / DIAG_CHUNK_ENTRIES)

static DiagEntry g_diag[DIAG_LOG_CAPACITY_ENTRIES];
static uint16_t g_head  = 0;
static uint16_t g_count = 0;

static unsigned long g_lastSampleMillis = 0;

static void persistChunk(uint16_t physicalIndex) {
  int chunk = physicalIndex / DIAG_CHUNK_ENTRIES;
  prefsDiag.begin("diaglog", false);
  prefsDiag.putUShort("head", g_head);
  prefsDiag.putUShort("count", g_count);
  String key = "c" + String(chunk);
  prefsDiag.putBytes(key.c_str(), &g_diag[chunk * DIAG_CHUNK_ENTRIES], DIAG_CHUNK_ENTRIES * sizeof(DiagEntry));
  prefsDiag.end();
}

// Añade una nueva muestra al buffer circular y la persiste en NVS.
// "resetReason" solo se rellena distinto de 0 en la muestra de arranque.
static void addEntry(uint8_t wsClients, uint8_t resetReason) {
  DiagEntry e;
  e.timestamp     = (uint32_t)time(nullptr);
  e.freeHeap      = (uint32_t)ESP.getFreeHeap();
  e.minFreeHeap   = (uint32_t)ESP.getMinFreeHeap();
  e.uptimeSec     = (uint32_t)(millis() / 1000UL);
  e.rssi          = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
  e.wsClients     = wsClients;
  e.wifiConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  e.resetReason   = resetReason;

  g_diag[g_head] = e;
  persistChunk(g_head);

  g_head = (g_head + 1) % DIAG_LOG_CAPACITY_ENTRIES;
  if (g_count < DIAG_LOG_CAPACITY_ENTRIES) g_count++;

  g_lastSampleMillis = millis();
}

void diaglogInit() {
  prefsDiag.begin("diaglog", true);
  g_head  = prefsDiag.getUShort("head", 0);
  g_count = prefsDiag.getUShort("count", 0);
  for (int chunk = 0; chunk < DIAG_NUM_CHUNKS; chunk++) {
    String key = "c" + String(chunk);
    size_t expected = DIAG_CHUNK_ENTRIES * sizeof(DiagEntry);
    size_t got = prefsDiag.getBytes(key.c_str(), &g_diag[chunk * DIAG_CHUNK_ENTRIES], expected);
    if (got != expected) {
      memset(&g_diag[chunk * DIAG_CHUNK_ENTRIES], 0, expected);
    }
  }
  prefsDiag.end();
  Serial.printf("[DIAG] Historico de diagnostico cargado: %u muestras\n", g_count);

  // Registro inmediato del arranque, con el motivo del ultimo reset. Esto
  // es lo mas importante para investigar cuelgues: si el reset fue por
  // watchdog (RTCWDT/TASK_WDT), panic, brownout, etc., queda constancia
  // aunque el timestamp aun no este sincronizado por NTP (saldra 0 y se
  // podra situar por uptime/orden en la lista).
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[DIAG] Motivo del ultimo arranque: %s\n", diaglogResetReasonText((uint8_t)reason));
  addEntry(0, (uint8_t)reason);
}

void diaglogLoop(uint8_t wsClients) {
  if (millis() - g_lastSampleMillis < DIAG_SAMPLE_INTERVAL_MS) return;
  addEntry(wsClients, 0);
}

int diaglogCount() {
  return g_count;
}

// Borra todo el historico: resetea cabecera y buffer en RAM, y limpia
// por completo el namespace NVS "diaglog" (mas simple y fiable que
// regrabar cada chunk a cero).
void diaglogClear() {
  g_head  = 0;
  g_count = 0;
  memset(g_diag, 0, sizeof(g_diag));

  prefsDiag.begin("diaglog", false);
  prefsDiag.clear();
  prefsDiag.end();

  Serial.println("[DIAG] Historico de diagnostico borrado");
}

DiagEntry diaglogGet(int index) {
  int physical;
  if (g_count < DIAG_LOG_CAPACITY_ENTRIES) {
    physical = index;
  } else {
    physical = (g_head + index) % DIAG_LOG_CAPACITY_ENTRIES;
  }
  return g_diag[physical];
}

String diaglogToJson() {
  int n = diaglogCount();
  String out;
  out.reserve(n * 48 + 16);
  out += "{\"samples\":[";
  for (int i = 0; i < n; i++) {
    DiagEntry e = diaglogGet(i);
    if (i > 0) out += ',';
    out += '[';
    out += e.timestamp;     out += ',';
    out += e.freeHeap;      out += ',';
    out += e.minFreeHeap;   out += ',';
    out += e.uptimeSec;     out += ',';
    out += e.rssi;          out += ',';
    out += e.wsClients;     out += ',';
    out += e.wifiConnected; out += ',';
    out += e.resetReason;
    out += ']';
  }
  out += "]}";
  return out;
}

const char* diaglogResetReasonText(uint8_t reason) {
  switch ((esp_reset_reason_t)reason) {
    case ESP_RST_POWERON:   return "Encendido normal";
    case ESP_RST_EXT:       return "Reset externo (pin RESET)";
    case ESP_RST_SW:        return "Reinicio por software (ESP.restart)";
    case ESP_RST_PANIC:     return "PANIC (excepcion/crash del firmware)";
    case ESP_RST_INT_WDT:   return "Watchdog interno (interrupcion bloqueada)";
    case ESP_RST_TASK_WDT:  return "Watchdog de tarea (loop/tarea colgada)";
    case ESP_RST_WDT:       return "Otro watchdog";
    case ESP_RST_DEEPSLEEP: return "Salida de deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout (caida de tension)";
    case ESP_RST_SDIO:      return "Reset via SDIO";
    default:                return "Desconocido";
  }
}
