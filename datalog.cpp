/*
 * datalog.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de registro historico como CLIENTE del
 * servidor remoto (ver datalog.h y jacuzzi_server/ en la raiz del repo).
 *
 * Diseño para no bloquear nunca el loop() principal (control de reles):
 *   - addEntry() solo mete la muestra en una cola FreeRTOS (xQueueSend
 *     sin esperar) y devuelve al instante.
 *   - Una tarea aparte (remoteLogTask, corriendo en su propio nucleo)
 *     es la unica que hace peticiones HTTP de verdad, con un timeout
 *     corto (REMOTE_LOG_HTTP_TIMEOUT_MS) para no quedarse colgada si el
 *     PC no responde.
 *   - Si el envio falla, la muestra se guarda en un buffer circular de
 *     emergencia en RAM (g_fallback) en vez de perderse sin mas. En
 *     cuanto el servidor vuelve a responder, esa tarea intenta vaciar
 *     primero el backlog acumulado.
 *   - datalogToJson() (llamada desde el contexto del servidor web, no
 *     desde el loop() de control) hace un GET sincrono con el mismo
 *     timeout corto; si falla, sirve el JSON construido a partir del
 *     buffer de emergencia para que la pagina "/datos" nunca se quede
 *     completamente en blanco.
 * -----------------------------------------------------------------------
 */
#include "datalog.h"
#include "data.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static unsigned long g_lastSampleMillis = 0;
static uint8_t       g_lastFlags        = 0xFF; // invalido a proposito: fuerza el primer registro

// ---------------- Cola de envio (loop() -> tarea HTTP) ----------------
static QueueHandle_t g_sendQueue = nullptr;

// ---------------- Buffer circular de emergencia (solo RAM, no NVS) ----------------
// Se usa unicamente mientras el servidor remoto no responde. A diferencia
// del antiguo buffer de 1000 entradas respaldado en NVS, este es pequeño
// a proposito: es solo un colchon para no perder datos durante un corte
// puntual del PC/red, no un almacen a largo plazo (eso ahora vive en el
// servidor). Protegido con mutex porque lo tocan dos contextos distintos:
// la tarea remoteLogTask() (al fallar un envio) y datalogToJson() (al
// servir /api/history si el servidor no responde en ese momento).
static LogEntry      g_fallback[REMOTE_LOG_FALLBACK_CAPACITY];
static uint16_t      g_fbHead  = 0;
static uint16_t      g_fbCount = 0;
static SemaphoreHandle_t g_fbMutex = nullptr;

static void fallbackPush(const LogEntry &e) {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  g_fallback[g_fbHead] = e;
  g_fbHead = (g_fbHead + 1) % REMOTE_LOG_FALLBACK_CAPACITY;
  if (g_fbCount < REMOTE_LOG_FALLBACK_CAPACITY) g_fbCount++;
  xSemaphoreGive(g_fbMutex);
}

// Construye el JSON {"samples":[[ts,t1,t2,flags],...]} a partir del
// buffer de emergencia. Mismo formato que devuelve el servidor, para que
// el cliente web (pagina /datos) no note la diferencia.
static String fallbackToJson() {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  int n = g_fbCount;
  String out;
  out.reserve(n * 26 + 16);
  out += "{\"samples\":[";
  for (int i = 0; i < n; i++) {
    int physical = (g_fbCount < REMOTE_LOG_FALLBACK_CAPACITY)
                     ? i
                     : (g_fbHead + i) % REMOTE_LOG_FALLBACK_CAPACITY;
    LogEntry e = g_fallback[physical];
    if (i > 0) out += ',';
    out += '[';
    out += e.timestamp;
    out += ',';
    out += (e.tJacuzziX10 / 10.0f);
    out += ',';
    out += (e.tSolarX10 / 10.0f);
    out += ',';
    out += e.flags;
    out += ']';
  }
  out += "]}";
  xSemaphoreGive(g_fbMutex);
  return out;
}

static void fallbackClear() {
  xSemaphoreTake(g_fbMutex, portMAX_DELAY);
  g_fbHead = 0;
  g_fbCount = 0;
  xSemaphoreGive(g_fbMutex);
}

// ---------------- Comunicacion HTTP con el servidor remoto ----------------

// Envia una muestra al servidor. Devuelve true si el servidor respondio
// con un codigo 2xx. Bloqueante, pero con timeout corto: solo se llama
// desde remoteLogTask() (su propia tarea), nunca desde el loop() principal.
static bool sendSampleHttp(const LogEntry &e) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  if (!http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/data")) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", REMOTE_LOG_API_KEY);

  char body[80];
  snprintf(body, sizeof(body), "{\"ts\":%u,\"t1\":%.1f,\"t2\":%.1f,\"flags\":%u}",
           (unsigned)e.timestamp, e.tJacuzziX10 / 10.0f, e.tSolarX10 / 10.0f, (unsigned)e.flags);

  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();
  return code >= 200 && code < 300;
}

// Tarea en segundo plano: unica responsable de hablar por HTTP con el
// servidor. Corre en el nucleo 0 (Arduino/loop() suele ir en el 1), para
// que un servidor lento nunca compita por CPU con el control de reles.
static void remoteLogTask(void *pv) {
  LogEntry e;
  for (;;) {
    // Espera bloqueada (sin gastar CPU) hasta que datalogLoop() encole algo.
    if (xQueueReceive(g_sendQueue, &e, portMAX_DELAY) != pdTRUE) continue;

    // Si hay backlog de emergencia pendiente, se intenta primero (en
    // orden cronologico) antes que la muestra recien encolada: asi el
    // historico que llega al servidor no queda con huecos "al reves".
    xSemaphoreTake(g_fbMutex, portMAX_DELAY);
    int backlog = g_fbCount;
    xSemaphoreGive(g_fbMutex);

    if (backlog > 0) {
      xSemaphoreTake(g_fbMutex, portMAX_DELAY);
      int physical = (g_fbCount < REMOTE_LOG_FALLBACK_CAPACITY)
                       ? 0
                       : g_fbHead;
      LogEntry oldest = g_fallback[physical];
      xSemaphoreGive(g_fbMutex);

      if (sendSampleHttp(oldest)) {
        // Exito: la mas antigua del backlog ya se envio, se quita del
        // buffer avanzando el indice de lectura.
        xSemaphoreTake(g_fbMutex, portMAX_DELAY);
        g_fbCount--;
        xSemaphoreGive(g_fbMutex);
      } else {
        // El servidor sigue sin responder: la muestra actual tambien se
        // guarda en el backlog (en vez de intentar enviarla ya) para no
        // desordenar el historico.
        fallbackPush(e);
        continue;
      }
    }

    if (!sendSampleHttp(e)) {
      Serial.println("[DATALOG] Servidor remoto no responde: muestra guardada en buffer local");
      fallbackPush(e);
    }
  }
}

void datalogInit() {
  g_fbMutex = xSemaphoreCreateMutex();
  g_sendQueue = xQueueCreate(REMOTE_LOG_QUEUE_LEN, sizeof(LogEntry));

  // xTaskCreatePinnedToCore: nucleo 0, prioridad baja (1), 4KB de stack
  // de sobra para HTTPClient. Reutilizable tal cual en otro proyecto que
  // necesite un "cliente HTTP en segundo plano" sin bloquear su loop().
  xTaskCreatePinnedToCore(remoteLogTask, "remoteLogTask", 4096, nullptr, 1, nullptr, 0);

  Serial.println("[DATALOG] Cliente del servidor remoto inicializado");
}

// Calcula los flags de estado actuales a partir de g_state.
static uint8_t currentFlags() {
  uint8_t f = 0;
  if (g_state.pumpOn)          f |= LOG_FLAG_PUMP;
  if (g_state.autoEnabled)     f |= LOG_FLAG_AUTO;
  if (g_state.valvulasActivas) f |= LOG_FLAG_VALVES;
  if (g_state.forceSolar)      f |= LOG_FLAG_FORCE_SOLAR;
  return f;
}

// Encola una nueva muestra para enviar al servidor remoto. No bloquea:
// si la cola estuviera llena (servidor muy lento durante mucho tiempo),
// se descarta la muestra mas nueva en vez de esperar.
static void addEntry(uint8_t flags) {
  LogEntry e;
  e.timestamp   = (uint32_t)time(nullptr);
  e.tJacuzziX10 = (int16_t)roundf(g_state.tJacuzzi * 10.0f);
  e.tSolarX10   = (int16_t)roundf(g_state.tSolar * 10.0f);
  e.flags       = flags;

  if (xQueueSend(g_sendQueue, &e, 0) != pdTRUE) {
    Serial.println("[DATALOG] Cola de envio llena, muestra descartada");
  }

  g_lastFlags = flags;
  g_lastSampleMillis = millis();
}

void datalogLoop() {
  // Sin hora sincronizada por NTP no tiene sentido registrar (el
  // timestamp saldria invalido). time(nullptr) por debajo de este umbral
  // significa que el reloj aun no se ha puesto en hora.
  if (time(nullptr) < 1600000000) return;

  uint8_t flags = currentFlags();

  bool timeToSample = (millis() - g_lastSampleMillis) >= LOG_SAMPLE_INTERVAL_MS;
  bool stateChanged = (flags != g_lastFlags);

  // Solo se registra en modo automatico: si el usuario esta operando en
  // manual (bomba/solar/filtracion a mano), no queremos que el historico
  // se ensucie con esas maniobras puntuales.
  if (!g_state.autoEnabled) return;

  if (timeToSample || stateChanged) {
    addEntry(flags);
    if (stateChanged) {
      Serial.println("[DATALOG] Evento registrado (cambio de estado)");
    }
  }
}

String datalogToJson() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
    if (http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/history")) {
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        http.end();
        return body;
      }
      http.end();
    }
  }
  // Servidor inalcanzable: se sirve lo que haya en el buffer de
  // emergencia para que la pagina "/datos" no se quede vacia del todo.
  Serial.println("[DATALOG] /api/history remoto no disponible, sirviendo buffer local");
  return fallbackToJson();
}

void datalogDeleteRange(uint32_t fromTs, uint32_t toTs) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[DATALOG] Sin WiFi: no se puede pedir el borrado al servidor remoto");
    return;
  }
  HTTPClient http;
  http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  String url = String(REMOTE_LOG_SERVER_URL) + "/api/history/deleteday?from=" + fromTs + "&to=" + toTs;
  if (http.begin(url)) {
    http.addHeader("X-API-Key", REMOTE_LOG_API_KEY);
    int code = http.POST("");
    http.end();
    Serial.printf("[DATALOG] Borrado remoto [%u,%u) -> HTTP %d\n", fromTs, toTs, code);
  }
}

void datalogFormat() {
  fallbackClear();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[DATALOG] Sin WiFi: no se puede pedir el formateo al servidor remoto");
    return;
  }
  HTTPClient http;
  http.setTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(REMOTE_LOG_HTTP_TIMEOUT_MS);
  if (http.begin(String(REMOTE_LOG_SERVER_URL) + "/api/history/format")) {
    http.addHeader("X-API-Key", REMOTE_LOG_API_KEY);
    int code = http.POST("");
    http.end();
    Serial.printf("[DATALOG] Formateo remoto -> HTTP %d\n", code);
  }
}
