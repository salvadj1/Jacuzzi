/*
 * diaglog.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de registro de diagnostico como CLIENTE del
 * servidor remoto (mismo servidor Python que datalog.cpp, endpoints
 * /api/diag). Ver diaglog.h para el diseño general.
 *
 * Igual patron que datalog.cpp: cola FreeRTOS + tarea en segundo plano
 * para no bloquear el loop() principal, y un pequeño buffer de
 * emergencia en RAM si el servidor no responde.
 *
 * Lo que SIGUE siendo local (a proposito, ver diaglog.h): el breadcrumb
 * de memoria RTC (g_rtcStage) y el pico de duracion de loop()
 * (g_maxLoopMicros). Ninguno de los dos depende de red ni del servidor,
 * precisamente porque su trabajo es registrar POR QUE se colgo el
 * firmware, y el momento en que eso pasa es el peor momento posible para
 * depender de que una peticion HTTP funcione.
 * -----------------------------------------------------------------------
 */
#include "diaglog.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <time.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Namespace propio en NVS: ya SOLO se usa para persistir el intervalo de
// muestreo (4 bytes), no el historico completo como antes.
static Preferences prefsDiag;

// Memoria RTC: sobrevive a resets por software/panic/watchdog/brownout
// (no a un corte de alimentacion real, donde vuelve a 0 = DIAG_STAGE_BOOT,
// lo cual es correcto: en ese caso no hay "loop colgado" que investigar).
static RTC_NOINIT_ATTR uint8_t g_rtcStage;

// Pico de duracion de loop() desde la ultima muestra registrada
static uint32_t g_maxLoopMicros = 0;

static unsigned long g_lastSampleMillis = 0;

// Intervalo de muestreo actual (arranca con el valor de config.h hasta
// que diaglogInit() lo sobreescriba con lo guardado en NVS, si lo hay).
static uint32_t g_intervalMs = DIAG_SAMPLE_INTERVAL_MS;

// ---------------- Cola de envio (loop() -> tarea HTTP) ----------------
static QueueHandle_t g_sendQueue = nullptr;

// ---------------- Buffer circular de emergencia (solo RAM) ----------------
// Igual patron que datalog.cpp: colchon pequeño para no perder muestras
// durante un corte puntual del servidor, protegido con mutex porque lo
// tocan tanto la tarea de envio como diaglogToJson() (contexto web).
static DiagEntry         g_fallback[DIAG_LOG_CAPACITY_ENTRIES];
static uint16_t          g_fbHead  = 0;
static uint16_t          g_fbCount = 0;
static SemaphoreHandle_t g_fbMutex = nullptr;

static void fallbackPush(const DiagEntry &e) {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  g_fallback[g_fbHead] = e;
  g_fbHead = (g_fbHead + 1) % DIAG_LOG_CAPACITY_ENTRIES;
  if (g_fbCount < DIAG_LOG_CAPACITY_ENTRIES) g_fbCount++;
  xSemaphoreGive(g_fbMutex);
}

static void fallbackClear() {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  g_fbHead = 0;
  g_fbCount = 0;
  xSemaphoreGive(g_fbMutex);
}

// ---------------- Textos / clasificacion (puramente locales, sin red) ----------------

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
    case DIAG_STAGE_NTP_TIMEOUT:  return "Reinicio por falta de hora NTP";
    case DIAG_STAGE_NTP_TIMEOUT_NOWIFI: return "Reinicio por falta de hora NTP (sin WiFi estable)";
    default:                      return "Desconocido";
  }
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

uint8_t diaglogEventClass(uint8_t reason, uint8_t breadcrumb) {
  switch ((esp_reset_reason_t)reason) {
    case ESP_RST_UNKNOWN: return 0; // muestra periodica (no de arranque): no es un evento
    case ESP_RST_POWERON: return 0;
    case ESP_RST_EXT:
    case ESP_RST_SDIO:    return 2;
    case ESP_RST_SW:
      return (breadcrumb == DIAG_STAGE_NTP_TIMEOUT || breadcrumb == DIAG_STAGE_NTP_TIMEOUT_NOWIFI) ? 1 : 2;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return 3;
    default: return 3; // codigo desconocido: mejor tratarlo como anomalo que ignorarlo
  }
}

// ---------------- JSON (compartido entre servidor remoto y fallback local) ----------------

// Construye el JSON de una muestra en el MISMO formato/orden de campos
// que devolvia el antiguo /api/diag local, para que diagpage.cpp no
// necesite ningun cambio:
// [ts,freeHeap,minFreeHeap,maxAllocHeap,uptimeSec,maxLoopMicros,
//   minStackBytes,rssi,wsClients,wifiConnected,resetReason,
//   resetReasonTexto,breadcrumb(texto),wifiReconnects,ntcErrors,eventClass]
static void appendEntryJson(String &out, const DiagEntry &e) {
  bool esEvento = (e.resetReason && e.resetReason != 1);
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
  out += '"'; out += esEvento ? diaglogResetReasonText(e.resetReason) : ""; out += '"'; out += ',';
  out += '"'; out += esEvento ? diaglogStageText(e.breadcrumb) : ""; out += '"'; out += ',';
  out += e.wifiReconnects; out += ',';
  out += e.ntcErrors;      out += ',';
  out += diaglogEventClass(e.resetReason, e.breadcrumb);
  out += ']';
}

static String fallbackToJson() {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  int n = g_fbCount;
  String out;
  out.reserve(n * 100 + 32);
  out += "{\"intervalMs\":";
  out += g_intervalMs;
  out += ",\"samples\":[";
  for (int i = 0; i < n; i++) {
    int physical = (g_fbCount < DIAG_LOG_CAPACITY_ENTRIES) ? i : (g_fbHead + i) % DIAG_LOG_CAPACITY_ENTRIES;
    if (i > 0) out += ',';
    appendEntryJson(out, g_fallback[physical]);
  }
  out += "]}";
  xSemaphoreGive(g_fbMutex);
  return out;
}

// ---------------- Comunicacion HTTP con el servidor remoto ----------------

// Envia una muestra de diagnostico al servidor (/api/diag). Igual que en
// datalog.cpp: timeout corto, bloqueante pero SOLO dentro de la tarea
// dedicada, nunca desde el loop() principal.
static bool sendDiagHttp(const DiagEntry &e) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  if (!http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/diag")) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", REMOTE_LOG_API_KEY);

  bool esEvento = (e.resetReason && e.resetReason != 1);
  String body;
  body.reserve(220);
  body += "{\"ts\":"; body += e.timestamp;
  body += ",\"freeHeap\":"; body += e.freeHeap;
  body += ",\"minFreeHeap\":"; body += e.minFreeHeap;
  body += ",\"maxAllocHeap\":"; body += e.maxAllocHeap;
  body += ",\"uptimeSec\":"; body += e.uptimeSec;
  body += ",\"maxLoopMicros\":"; body += e.maxLoopMicros;
  body += ",\"minStackBytes\":"; body += e.minStackBytes;
  body += ",\"rssi\":"; body += e.rssi;
  body += ",\"wsClients\":"; body += e.wsClients;
  body += ",\"wifiConnected\":"; body += e.wifiConnected;
  body += ",\"resetReason\":"; body += e.resetReason;
  body += ",\"resetReasonText\":\""; body += (esEvento ? diaglogResetReasonText(e.resetReason) : ""); body += '"';
  body += ",\"breadcrumbText\":\""; body += (esEvento ? diaglogStageText(e.breadcrumb) : ""); body += '"';
  body += ",\"wifiReconnects\":"; body += e.wifiReconnects;
  body += ",\"ntcErrors\":"; body += e.ntcErrors;
  body += ",\"eventClass\":"; body += diaglogEventClass(e.resetReason, e.breadcrumb);
  body += '}';

  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 300;
}

static void remoteDiagTask(void *pv) {
  DiagEntry e;
  for (;;) {
    if (xQueueReceive(g_sendQueue, &e, portMAX_DELAY) != pdTRUE) continue;

    // Igual que en datalog.cpp: si hay backlog de emergencia, se intenta
    // primero en orden cronologico antes que la muestra recien encolada.
    xSemaphoreTake(g_fbMutex, portMAX_DELAY);
    int backlog = g_fbCount;
    xSemaphoreGive(g_fbMutex);

    if (backlog > 0) {
      xSemaphoreTake(g_fbMutex, portMAX_DELAY);
      int physical = (g_fbCount < DIAG_LOG_CAPACITY_ENTRIES) ? 0 : g_fbHead;
      DiagEntry oldest = g_fallback[physical];
      xSemaphoreGive(g_fbMutex);

      if (sendDiagHttp(oldest)) {
        xSemaphoreTake(g_fbMutex, portMAX_DELAY);
        g_fbCount--;
        xSemaphoreGive(g_fbMutex);
      } else {
        fallbackPush(e);
        continue;
      }
    }

    if (!sendDiagHttp(e)) {
      fallbackPush(e);
    }
  }
}

// ---------------- API publica ----------------

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

  if (xQueueSend(g_sendQueue, &e, 0) != pdTRUE) {
    Serial.println("[DIAG] Cola de envio llena, muestra descartada");
  }

  g_lastSampleMillis = millis();
  g_maxLoopMicros = 0; // arranca de cero para medir el siguiente periodo
}

void diaglogInit() {
  g_fbMutex = xSemaphoreCreateMutex();
  g_sendQueue = xQueueCreate(REMOTE_LOG_QUEUE_LEN, sizeof(DiagEntry));
  xTaskCreatePinnedToCore(remoteDiagTask, "remoteDiagTask", 4096, nullptr, 1, nullptr, 0);

  // El intervalo de muestreo es lo UNICO que se sigue guardando en NVS:
  // es un ajuste (4 bytes), no el historico completo.
  prefsDiag.begin("diaglog", true);
  g_intervalMs = prefsDiag.getUInt("intervalMs", DIAG_SAMPLE_INTERVAL_MS);
  prefsDiag.end();

  Serial.println("[DIAG] Cliente del servidor remoto inicializado");

  // Registro inmediato del arranque, con el motivo del ultimo reset. Esto
  // es lo mas importante para investigar cuelgues: si el reset fue por
  // watchdog (RTCWDT/TASK_WDT), panic, brownout, etc., queda constancia
  // aunque el timestamp aun no este sincronizado por NTP (saldra 0) ni
  // haya WiFi todavia (se encola y se envia en cuanto lo haya).
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[DIAG] Motivo del ultimo arranque: %s\n", diaglogResetReasonText((uint8_t)reason));

  // El breadcrumb solo es fiable si el reset vino de un cuelgue real
  // (panic/watchdog/brownout) o de un reinicio deliberado por software
  // (ESP.restart(), p.ej. el de NTP_TIMEOUT en el .ino); en un encendido
  // normal o reset externo la RTC RAM puede traer basura de la sesion
  // anterior sin relacion.
  uint8_t crumb = DIAG_STAGE_BOOT;
  if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
      reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
      reason == ESP_RST_BROWNOUT || reason == ESP_RST_SW) {
    crumb = g_rtcStage;
    Serial.printf("[DIAG] Se quedo colgado en: %s\n", diaglogStageText(crumb));
  }
  g_rtcStage = DIAG_STAGE_BOOT;

  addEntry(0, (uint8_t)reason, crumb, 0, 0);
}

void diaglogLoop(uint8_t wsClients, uint16_t wifiReconnects, uint16_t ntcErrors) {
  if (millis() - g_lastSampleMillis < g_intervalMs) return;
  addEntry(wsClients, 0, DIAG_STAGE_BOOT, wifiReconnects, ntcErrors);
}

uint32_t diaglogGetIntervalMs() {
  return g_intervalMs;
}

void diaglogSetIntervalMs(uint32_t ms) {
  uint32_t minMs = DIAG_INTERVAL_MIN_MINUTES * 60UL * 1000UL;
  uint32_t maxMs = DIAG_INTERVAL_MAX_MINUTES * 60UL * 1000UL;
  if (ms < minMs) ms = minMs;
  if (ms > maxMs) ms = maxMs;
  g_intervalMs = ms;

  prefsDiag.begin("diaglog", false);
  prefsDiag.putUInt("intervalMs", g_intervalMs);
  prefsDiag.end();

  Serial.printf("[DIAG] Intervalo de muestreo cambiado a %u ms\n", g_intervalMs);
}

void diaglogRecordLoopDuration(uint32_t micros_duration) {
  if (micros_duration > g_maxLoopMicros) g_maxLoopMicros = micros_duration;
}

void diaglogSetStage(uint8_t stage) {
  g_rtcStage = stage;
}

String diaglogToJson() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
    if (http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/diag")) {
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        http.end();
        return body;
      }
      http.end();
    }
  }
  Serial.println("[DIAG] /api/diag remoto no disponible, sirviendo buffer local");
  return fallbackToJson();
}

void diaglogClear() {
  fallbackClear();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[DIAG] Sin WiFi: no se puede pedir el borrado al servidor remoto");
    return;
  }
  HTTPClient http;
  http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  if (http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/diag/clear")) {
    http.addHeader("X-API-Key", REMOTE_LOG_API_KEY);
    int code = http.POST("");
    http.end();
    Serial.printf("[DIAG] Borrado remoto -> HTTP %d\n", code);
  }
}
