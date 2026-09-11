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
#include <esp_task_wdt.h>

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

// Contador de muestras añadidas en RAM desde el ultimo volcado a NVS.
// Ver DATALOG_PERSIST_EVERY_N en datalog.h: reduce la frecuencia de
// escritura en flash, que era la causa confirmada de resets por
// watchdog/panic dentro de datalogLoop (ver diagnostico /diag).
static uint8_t g_samplesSinceFlush = 0;

// Buffer temporal solo para la relectura de verificacion tras escribir un
// trozo. Estatico (no en stack) por el mismo motivo que g_logTmp: es chico
// (900 bytes) pero evitamos tocar el stack de la tarea principal.
static LogEntry g_verifyBuf[LOG_CHUNK_ENTRIES];

// Escribe el trozo "chunk" en NVS a partir de g_log y relee inmediatamente
// para comprobar que quedo bien grabado (proteccion frente a un corte de
// alimentacion/reset a mitad de la escritura en flash). Asume que
// prefsLog.begin() ya esta abierto por el llamante.
//
// Reutilizable: la usan tanto el volcado periodico (persistChunk) como el
// borrado/compactado completo (datalogDeleteRange), y serviria igual para
// cualquier otro buffer circular con este mismo patron de chunks (mismo
// patron que diaglog.cpp).
//
// Devuelve true si el trozo quedo escrito y verificado correctamente.
static bool writeChunkVerified(int chunk) {
  size_t chunkBytes = LOG_CHUNK_ENTRIES * sizeof(LogEntry);
  LogEntry* chunkData = &g_log[chunk * LOG_CHUNK_ENTRIES];

  // Clave fija en stack (sin String): se llama en cada volcado a NVS, y
  // usar String aqui iba fragmentando el heap con el tiempo hasta
  // provocar un panic.
  char key[4];
  snprintf(key, sizeof(key), "c%d", chunk);

  prefsLog.putBytes(key, chunkData, chunkBytes);

  // Relectura de verificacion: si no coincide (o directamente no se puede
  // leer de vuelta), la escritura no es de fiar.
  size_t got = prefsLog.getBytes(key, g_verifyBuf, chunkBytes);
  bool ok = (got == chunkBytes) && (memcmp(g_verifyBuf, chunkData, chunkBytes) == 0);

  if (!ok) {
    Serial.printf("[DATALOG] AVISO: fallo al verificar el trozo %d (%s) en NVS\n", chunk, key);
  }
  return ok;
}

// Vuelca a NVS solo el trozo que contiene "physicalIndex", mas la cabecera
// (head/count). Minimiza el desgaste de flash frente a regrabar todo el
// buffer en cada muestra.
//
// BLINDAJE ante corte a mitad de escritura (reset/brownout, p.ej. al
// conmutar un rele justo cuando toca volcar a flash): el trozo de datos
// se escribe y se verifica (ver writeChunkVerified). Solo si la
// verificacion pasa se actualizan "head"/"count" en NVS. Si no, esos
// punteros se quedan como estaban (el ultimo estado bueno ya guardado) y
// NO se toca el trozo con datos corruptos: datalogInit() nunca los vera
// como validos porque count no llego a incluirlos. En el peor caso se
// pierden, en RAM, solo las muestras pendientes de ESTE volcado (se
// reintenta en el siguiente), nunca un dia entero ya persistido.
//
// Se "alimenta" el watchdog antes y despues de la escritura en flash:
// aunque ahora se llama con mucha menos frecuencia (ver
// DATALOG_PERSIST_EVERY_N), esto actua como red de seguridad ante una
// escritura NVS puntual mas lenta de lo normal (flash desgastada, etc.),
// evitando que dispare un reset por watchdog en mitad de la operacion.
//
// Devuelve true si el trozo quedo escrito y verificado correctamente.
static bool persistChunk(uint16_t physicalIndex) {
  int chunk = physicalIndex / LOG_CHUNK_ENTRIES;

  esp_task_wdt_reset();
  prefsLog.begin("datalog", false);
  bool ok = writeChunkVerified(chunk);
  if (ok) {
    prefsLog.putUShort("head", g_head);
    prefsLog.putUShort("count", g_count);
  }
  prefsLog.end();
  esp_task_wdt_reset();

  if (!ok) {
    Serial.println("[DATALOG] head/count NO actualizados; se reintentara en el siguiente volcado");
  }
  return ok;
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
      // Si "count" decia que en este trozo debia haber datos reales, esto
      // no es un primer arranque: es un trozo corrupto/ilegible y se
      // estan perdiendo muestras ya dadas por guardadas. De momento se
      // sigue recuperando igual (a cero), pero al menos queda avisado.
      if ((uint32_t)chunk * LOG_CHUNK_ENTRIES < g_count) {
        Serial.printf("[DATALOG] AVISO: trozo %d corrupto o ilegible al arrancar "
                      "(se esperaban datos, count=%u); esas muestras se pierden\n",
                      chunk, g_count);
      }
      // Trozo nunca escrito (primer arranque) o corrupto: lo dejamos a cero.
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

  // IMPORTANTE: se guarda la posicion fisica ANTES de avanzar g_head,
  // porque persistChunk() necesita saber que trozo contiene la muestra
  // que se acaba de escribir (no el siguiente hueco libre).
  uint16_t writtenAt = g_head;
  g_head = (g_head + 1) % LOG_CAPACITY;
  if (g_count < LOG_CAPACITY) g_count++;

  // Solo se vuelca a NVS cada DATALOG_PERSIST_EVERY_N muestras (o si el
  // buffer aun no se ha inicializado con ninguna previa), no en cada una:
  // esto es lo que reduce la frecuencia de escritura en flash. El resto
  // del tiempo la muestra vive solo en RAM (g_log) hasta el proximo
  // volcado.
  //
  // FIX: persistChunk() se llama AHORA (tras incrementar g_head/g_count),
  // no antes. Antes se llamaba con los valores VIEJOS, asi que el head/
  // count guardado en NVS quedaba permanentemente 1 muestra por detras
  // de la realidad, y cada reinicio volvia a escribir (pisar) la ultima
  // muestra que ya se habia guardado bien.
  g_samplesSinceFlush++;
  bool shouldFlush = (g_samplesSinceFlush >= DATALOG_PERSIST_EVERY_N);
  if (shouldFlush) {
    // Si la verificacion falla NO se resetea el contador: se reintentara
    // el volcado en la siguiente muestra en vez de darlo por hecho.
    if (persistChunk(writtenAt)) {
      g_samplesSinceFlush = 0;
    }
  }

  g_lastFlags = flags;
  g_lastSampleMillis = millis();
}

// Antiguedad maxima que se conserva en el historico. Sin esto, muestras
// de una sesion de pruebas muy anterior (ej. semanas atras, si el equipo
// ha estado mucho tiempo apagado entre sesiones) se quedan en el buffer
// para siempre, apareciendo como un dia "fantasma" suelto en la grafica
// aunque el uso normal solo lleve unos pocos dias.
#define LOG_MAX_AGE_SEC (10UL * 24UL * 3600UL) // 10 dias

// La purga reescribe TODO el historico en NVS (ver datalogDeleteRange),
// asi que se comprueba con poca frecuencia, no en cada vuelta del loop.
static unsigned long g_lastPruneCheckMs = 0;
#define LOG_PRUNE_CHECK_INTERVAL_MS (3600UL * 1000UL) // como mucho 1 vez/hora

// Comprueba si la muestra mas antigua supera LOG_MAX_AGE_SEC y, si es
// asi, borra todo lo anterior al corte. Reutiliza datalogDeleteRange()
// (el mismo borrado por rango que usa el boton "BORRAR DIA" de la web).
static void datalogPruneOldIfNeeded() {
  unsigned long now = millis();
  if (now - g_lastPruneCheckMs < LOG_PRUNE_CHECK_INTERVAL_MS) return;
  g_lastPruneCheckMs = now;

  if (g_count == 0) return;
  uint32_t nowEpoch = (uint32_t)time(nullptr);

  LogEntry oldest = datalogGet(0);
  if (oldest.timestamp == 0) return; // entrada invalida, no arriesgarse a borrar de mas
  if ((uint32_t)(nowEpoch - oldest.timestamp) <= LOG_MAX_AGE_SEC) return; // aun no toca

  uint32_t cutoff = nowEpoch - LOG_MAX_AGE_SEC;
  Serial.printf("[DATALOG] Purga automatica: borrando muestras de mas de %lu dias\n",
                LOG_MAX_AGE_SEC / 86400UL);
  datalogDeleteRange(0, cutoff); // borra [0, cutoff): todo lo anterior al corte
}

void datalogLoop() {
  // Sin hora sincronizada por NTP no tiene sentido registrar (el
  // timestamp saldria invalido). time(nullptr) por debajo de este umbral
  // significa que el reloj aun no se ha puesto en hora.
  if (time(nullptr) < 1600000000) return;

  datalogPruneOldIfNeeded();

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

// Borrado total de emergencia: limpia el buffer en RAM y borra por
// completo el namespace NVS "datalog" (mas fiable que reescribir chunk
// a chunk si hay datos corruptos que datalogDeleteRange no consigue
// identificar bien).
void datalogFormat() {
  memset(g_log, 0, sizeof(g_log));
  g_head  = 0;
  g_count = 0;
  g_lastFlags = 0xFF;
  g_lastSampleMillis = 0;
  g_samplesSinceFlush = 0;

  prefsLog.begin("datalog", false);
  prefsLog.clear();
  prefsLog.end();

  Serial.println("[DATALOG] Historico formateado por completo");
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
  uint16_t newCount = (uint16_t)kept;
  uint16_t newHead  = (uint16_t)(kept % LOG_CAPACITY);

  // BLINDAJE (mismo criterio que persistChunk): se escriben y verifican
  // TODOS los trozos antes de tocar head/count. Si alguno falla, head/
  // count en NVS se quedan como estaban (el ultimo estado bueno) y el
  // borrado NO se da por persistido -aunque en RAM ya haya quedado
  // aplicado, asi que el jacuzzi sigue funcionando bien con los datos ya
  // compactados-; queda avisado por Serial y basta con repetir el
  // borrado para reintentar guardarlo en NVS.
  esp_task_wdt_reset();
  prefsLog.begin("datalog", false);
  bool allOk = true;
  for (int chunk = 0; chunk < LOG_NUM_CHUNKS; chunk++) {
    if (!writeChunkVerified(chunk)) allOk = false;
    esp_task_wdt_reset(); // el borrado reescribe TODOS los trozos: vigilar el watchdog en cada uno
  }
  if (allOk) {
    prefsLog.putUShort("head", newHead);
    prefsLog.putUShort("count", newCount);
  }
  prefsLog.end();
  esp_task_wdt_reset();

  g_head  = newHead;
  g_count = newCount;
  g_samplesSinceFlush = 0; // esta operacion ya ha volcado (o intentado volcar) todo el buffer

  if (allOk) {
    Serial.printf("[DATALOG] Borradas muestras del rango [%u,%u); quedan %d\n", fromTs, toTs, g_count);
  } else {
    Serial.printf("[DATALOG] AVISO: borrado de rango [%u,%u) aplicado en RAM pero NO verificado "
                  "por completo en NVS; repite el borrado para reintentar guardarlo\n", fromTs, toTs);
  }
}