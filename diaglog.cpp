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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h> // uxTaskGetStackHighWaterMark

// Namespace propio en NVS para no mezclar con datalog.cpp ni storage.cpp
static Preferences prefsDiag;

// Memoria RTC: sobrevive a resets por software/panic/watchdog/brownout
// (no a un corte de alimentacion real, donde vuelve a 0 = DIAG_STAGE_BOOT,
// lo cual es correcto: en ese caso no hay "loop colgado" que investigar).
static RTC_NOINIT_ATTR uint8_t g_rtcStage;

// Pico de duracion de loop() desde la ultima muestra registrada
static uint32_t g_maxLoopMicros = 0;

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
// "resetReason" y "breadcrumb" solo se rellenan en la muestra de arranque.
static void addEntry(uint8_t wsClients, uint8_t resetReason, uint8_t breadcrumb,
                      uint16_t wifiReconnects, uint16_t ntcErrors) {
  DiagEntry e;
  e.timestamp       = (uint32_t)time(nullptr);
  e.freeHeap        = (uint32_t)ESP.getFreeHeap();
  e.minFreeHeap     = (uint32_t)ESP.getMinFreeHeap();
  e.maxAllocHeap    = (uint32_t)ESP.getMaxAllocHeap();
  e.uptimeSec       = (uint32_t)(millis() / 1000UL);
  e.maxLoopMicros   = g_maxLoopMicros;
  e.minStackBytes   = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
  e.rssi            = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
  e.wsClients       = wsClients;
  e.wifiConnected   = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  e.resetReason     = resetReason;
  e.breadcrumb      = breadcrumb;
  e.wifiReconnects  = wifiReconnects;
  e.ntcErrors       = ntcErrors;

  g_diag[g_head] = e;
  persistChunk(g_head);

  g_head = (g_head + 1) % DIAG_LOG_CAPACITY_ENTRIES;
  if (g_count < DIAG_LOG_CAPACITY_ENTRIES) g_count++;

  g_lastSampleMillis = millis();
  g_maxLoopMicros = 0; // arranca de cero para medir el siguiente periodo
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

  // El breadcrumb solo es fiable si el reset vino de un cuelgue real
  // (panic/watchdog/brownout); en un encendido normal o reset externo la
  // RTC RAM puede traer basura de la sesion anterior sin relacion.
  uint8_t crumb = DIAG_STAGE_BOOT;
  if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
      reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
      reason == ESP_RST_BROWNOUT) {
    crumb = g_rtcStage;
    Serial.printf("[DIAG] Se quedo colgado en: %s\n", diaglogStageText(crumb));
  }
  g_rtcStage = DIAG_STAGE_BOOT;

  addEntry(0, (uint8_t)reason, crumb, 0, 0);
}

void diaglogLoop(uint8_t wsClients, uint16_t wifiReconnects, uint16_t ntcErrors) {
  if (millis() - g_lastSampleMillis < DIAG_SAMPLE_INTERVAL_MS) return;
  addEntry(wsClients, 0, DIAG_STAGE_BOOT, wifiReconnects, ntcErrors);
}

void diaglogRecordLoopDuration(uint32_t micros_duration) {
  if (micros_duration > g_maxLoopMicros) g_maxLoopMicros = micros_duration;
}

void diaglogSetStage(uint8_t stage) {
  g_rtcStage = stage;
}

const char* diaglogStageText(uint8_t stage) {
  switch ((DiagStage)stage) {
    case DIAG_STAGE_BOOT:         return "Arranque";
    case DIAG_STAGE_LOOP_WIFI:    return "loopWifi";
    case DIAG_STAGE_OTA:          return "loopOta";
    case DIAG_STAGE_TEMP_SENSORS: return "loopTempSensors";
    case DIAG_STAGE_SCHEDULE:     return "loopSchedule";
    case DIAG_STAGE_DATALOG:      return "datalogLoop";
    case DIAG_STAGE_WEBSERVER:    return "webServerLoop";
    case DIAG_STAGE_DIAGLOG:      return "diaglogLoop";
    case DIAG_STAGE_BROADCAST:    return "broadcastState";
    default:                      return "Desconocido";
  }
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

// Indices del array por muestra (deben coincidir con diagpage.cpp):
// 0 timestamp, 1 freeHeap, 2 minFreeHeap, 3 maxAllocHeap, 4 uptimeSec,
// 5 maxLoopMicros, 6 minStackBytes, 7 rssi, 8 wsClients, 9 wifiConnected,
// 10 resetReason, 11 breadcrumb(texto), 12 wifiReconnects, 13 ntcErrors
String diaglogToJson() {
  int n = diaglogCount();
  String out;
  out.reserve(n * 80 + 16);
  out += "{\"samples\":[";
  for (int i = 0; i < n; i++) {
    DiagEntry e = diaglogGet(i);
    if (i > 0) out += ',';
    out += '[';
    out += e.timestamp;      out += ',';
    out += e.freeHeap;       out += ',';
    out += e.minFreeHeap;    out += ',';
    out += e.maxAllocHeap;   out += ',';
    out += e.uptimeSec;      out += ',';
    out += e.maxLoopMicros;  out += ',';
    out += e.minStackBytes;  out += ',';
    out += e.rssi;           out += ',';
    out += e.wsClients;      out += ',';
    out += e.wifiConnected;  out += ',';
    out += e.resetReason;    out += ',';
    out += '"'; out += (e.resetReason && e.resetReason != 1) ? diaglogStageText(e.breadcrumb) : ""; out += '"'; out += ',';
    out += e.wifiReconnects; out += ',';
    out += e.ntcErrors;
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
