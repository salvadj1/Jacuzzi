/*
 * datalog.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de registro historico. Ver datalog.h para
 * el diseño general (buffer circular en RAM + respaldo en NVS por
 * trozos/"chunks").
 * -----------------------------------------------------------------------
 */
#include "datalog.h"
#include "data.h"
#include "config.h"
#include <Preferences.h>
#include <time.h>
#include <string.h>

// Namespace propio en NVS para no mezclar con storage.cpp
static Preferences prefsLog;

// El buffer se guarda en NVS partido en trozos, porque una unica entrada
// NVS no admite blobs grandes. Cada trozo cabe de sobra dentro del limite.
#define LOG_CHUNK_ENTRIES 100
#define LOG_NUM_CHUNKS (LOG_CAPACITY / LOG_CHUNK_ENTRIES)

static LogEntry g_log[LOG_CAPACITY];
static uint16_t g_head  = 0; // proxima posicion fisica a escribir
static uint16_t g_count = 0; // muestras validas actualmente guardadas

static unsigned long g_lastSampleMillis = 0;
static uint8_t       g_lastFlags        = 0xFF; // invalido a proposito: fuerza el primer registro

// Vuelca a NVS solo el trozo que contiene "physicalIndex", mas la cabecera
// (head/count). Minimiza el desgaste de flash frente a regrabar todo el
// buffer en cada muestra.
static void persistChunk(uint16_t physicalIndex) {
  int chunk = physicalIndex / LOG_CHUNK_ENTRIES;
  prefsLog.begin("datalog", false);
  prefsLog.putUShort("head", g_head);
  prefsLog.putUShort("count", g_count);
  // Clave fija en stack (sin String): esto se llama en cada muestra
  // (cada 15 min o en cada cambio de estado), y usar String aqui iba
  // fragmentando el heap con el tiempo hasta provocar un panic.
  char key[4];
  snprintf(key, sizeof(key), "c%d", chunk);
  prefsLog.putBytes(key, &g_log[chunk * LOG_CHUNK_ENTRIES], LOG_CHUNK_ENTRIES * sizeof(LogEntry));
  prefsLog.end();
}

void datalogInit() {
  prefsLog.begin("datalog", true);
  g_head  = prefsLog.getUShort("head", 0);
  g_count = prefsLog.getUShort("count", 0);
  for (int chunk = 0; chunk < LOG_NUM_CHUNKS; chunk++) {
    String key = "c" + String(chunk);
    size_t expected = LOG_CHUNK_ENTRIES * sizeof(LogEntry);
    size_t got = prefsLog.getBytes(key.c_str(), &g_log[chunk * LOG_CHUNK_ENTRIES], expected);
    if (got != expected) {
      // Trozo nunca escrito (primer arranque): lo dejamos a cero.
      memset(&g_log[chunk * LOG_CHUNK_ENTRIES], 0, expected);
    }
  }
  prefsLog.end();
  Serial.printf("[DATALOG] Historico cargado: %u muestras\n", g_count);
}

// Calcula los flags de estado actuales a partir de g_state.
static uint8_t currentFlags() {
  uint8_t f = 0;
  if (g_state.pumpOn)         f |= LOG_FLAG_PUMP;
  if (g_state.autoEnabled)    f |= LOG_FLAG_AUTO;
  if (g_state.valvulasActivas) f |= LOG_FLAG_VALVES;
  if (g_state.forceSolar)     f |= LOG_FLAG_FORCE_SOLAR;
  return f;
}

// Añade una nueva muestra al buffer circular y la persiste en NVS.
static void addEntry(uint8_t flags) {
  LogEntry e;
  e.timestamp   = (uint32_t)time(nullptr);
  e.tJacuzziX10 = (int16_t)roundf(g_state.tJacuzzi * 10.0f);
  e.tSolarX10   = (int16_t)roundf(g_state.tSolar * 10.0f);
  e.flags       = flags;

  g_log[g_head] = e;
  persistChunk(g_head);

  g_head = (g_head + 1) % LOG_CAPACITY;
  if (g_count < LOG_CAPACITY) g_count++;

  g_lastFlags = flags;
  g_lastSampleMillis = millis();
}

void datalogLoop() {
  // Sin hora sincronizada por NTP no tiene sentido registrar (el
  // timestamp saldria invalido). time(nullptr) por debajo de este umbral
  // significa que el reloj aun no se ha puesto en hora.
  if (time(nullptr) < 1600000000) return;

  uint8_t flags = currentFlags();

  bool timeToSample  = (millis() - g_lastSampleMillis) >= LOG_SAMPLE_INTERVAL_MS;
  bool stateChanged  = (flags != g_lastFlags);

  if (timeToSample || stateChanged) {
    addEntry(flags);
    if (stateChanged) {
      Serial.println("[DATALOG] Evento registrado (cambio de estado)");
    }
  }
}

int datalogCount() {
  return g_count;
}

LogEntry datalogGet(int index) {
  int physical;
  if (g_count < LOG_CAPACITY) {
    physical = index; // el buffer aun no ha dado la vuelta: orden fisico = orden cronologico
  } else {
    physical = (g_head + index) % LOG_CAPACITY; // g_head es la mas antigua cuando esta lleno
  }
  return g_log[physical];
}

String datalogToJson() {
  int n = datalogCount();
  String out;
  out.reserve(n * 26 + 16);
  out += "{\"samples\":[";
  for (int i = 0; i < n; i++) {
    LogEntry e = datalogGet(i);
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
  return out;
}

// Buffer temporal estatico para compactar el historico al borrar un
// rango. Estatico en vez de malloc/new a proposito: es una operacion
// bajo demanda (poco frecuente) y asi no fragmenta el heap ni compite
// con el resto del firmware por memoria dinamica.
static LogEntry g_logTmp[LOG_CAPACITY];

// Borra todas las muestras cuyo timestamp cae en [fromTs, toTs) y
// compacta el resto al principio del buffer, reescribiendo todo el
// historico en NVS. Pensado para el boton "borrar este dia" de la web.
void datalogDeleteRange(uint32_t fromTs, uint32_t toTs) {
  int n = g_count;
  int kept = 0;
  for (int i = 0; i < n; i++) {
    LogEntry e = datalogGet(i);
    if (e.timestamp < fromTs || e.timestamp >= toTs) {
      g_logTmp[kept++] = e;
    }
  }

  memset(g_log, 0, sizeof(g_log));
  memcpy(g_log, g_logTmp, kept * sizeof(LogEntry));
  // Buffer recien compactado: nunca ha "dado la vuelta", asi que el
  // criterio es el mismo que en datalogGet() para ese caso (head=count).
  g_count = kept;
  g_head  = kept % LOG_CAPACITY;

  prefsLog.begin("datalog", false);
  prefsLog.putUShort("head", g_head);
  prefsLog.putUShort("count", g_count);
  for (int chunk = 0; chunk < LOG_NUM_CHUNKS; chunk++) {
    char key[4];
    snprintf(key, sizeof(key), "c%d", chunk);
    prefsLog.putBytes(key, &g_log[chunk * LOG_CHUNK_ENTRIES], LOG_CHUNK_ENTRIES * sizeof(LogEntry));
  }
  prefsLog.end();

  Serial.printf("[DATALOG] Borradas muestras del rango [%u,%u); quedan %d\n", fromTs, toTs, g_count);
}
