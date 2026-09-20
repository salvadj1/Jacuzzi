/*
 * diaglog.h
 * -----------------------------------------------------------------------
 * Modulo de registro de DIAGNOSTICO, independiente del datalog de
 * temperaturas. Su unico proposito es dejar rastro de "salud" del
 * sistema (memoria, WiFi, clientes web, motivo de arranque) para poder
 * investigar cuelgues o reinicios inesperados a posteriori, consultando
 * el historico desde la pagina web en vez de tener que estar delante
 * del Monitor Serie en el momento exacto en que ocurre el problema.
 *
 * Guarda una muestra cada intervalo configurable y ademas registra un
 * evento inmediato en el arranque (con el motivo del ultimo reset).
 *
 * MIGRACION: al igual que datalog.cpp, el historico de estas muestras ya
 * NO se guarda en el ESP32 (antes: buffer circular en RAM + NVS). Ahora
 * se envia por HTTP al mismo servidor Python que el datalog (ver
 * jacuzzi_server/), a los endpoints /api/diag. Libera toda la RAM/NVS
 * que antes ocupaba este buffer (200 x 32 bytes = 6,4 KB) y ya no limita
 * el historico a ~16-17 horas.
 *
 * IMPORTANTE: lo unico que SIGUE siendo local (y debe seguir siendolo) es
 * el breadcrumb de memoria RTC (diaglogSetStage) y el pico de duracion de
 * loop() (diaglogRecordLoopDuration): si el ESP32 se cuelga, el propio
 * mecanismo que registra el motivo del cuelgue no puede depender de que
 * la red/el servidor esten disponibles en ese instante.
 *
 * Reutilizable: no depende de nada especifico del jacuzzi salvo de leer
 * el numero de clientes WebSocket conectados (se le pasa por parametro
 * desde web_server.cpp, no aqui).
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Una muestra de diagnostico (32 bytes, sin padding gracias a "packed").
// Nota: al crecer la estructura, el historico guardado en NVS con el
// tamaño antiguo se descarta solo (ver diaglogInit): no hace falta migrar.
struct __attribute__((packed)) DiagEntry {
  uint32_t timestamp;     // Epoch (segundos), 0 = aun sin hora sincronizada
  uint32_t freeHeap;      // Heap libre en el momento de la muestra (bytes)
  uint32_t minFreeHeap;   // Heap libre minimo historico desde el arranque (bytes)
  uint32_t maxAllocHeap;  // Mayor bloque asignable de un tiron (ESP.getMaxAllocHeap).
                          // Si es mucho menor que freeHeap, el heap esta fragmentado
                          // y un malloc grande puede fallar aunque "haya heap libre".
  uint32_t uptimeSec;     // Segundos desde el ultimo arranque
  uint32_t maxLoopMicros; // Duracion de la vuelta de loop() mas lenta desde la
                          // muestra anterior (microsegundos). Picos altos anticipan
                          // un reinicio por watchdog (algo bloqueo el loop).
  uint32_t minStackBytes; // Stack libre minimo de la tarea principal desde el
                          // arranque (uxTaskGetStackHighWaterMark). Cerca de 0 =
                          // riesgo de stack overflow (crash dificil de explicar).
  int8_t   rssi;          // Nivel de señal WiFi (dBm), 0 si no aplica
  uint8_t  wsClients;     // Clientes WebSocket conectados en ese momento
  uint8_t  wifiConnected; // 1 = conectado a red domestica, 0 = no
  uint8_t  resetReason;   // Motivo de arranque (esp_reset_reason_t), solo valido en la muestra de arranque
  uint8_t  breadcrumb;    // Ultima "zona" del loop() en marcha antes de este reset
                          // (ver DiagStage). Solo tiene sentido en la muestra de
                          // arranque tras un PANIC/watchdog/brownout.
  uint16_t wifiReconnects;// Reconexiones WiFi acumuladas desde el arranque
  uint16_t ntcErrors;     // Lecturas NTC fuera de rango/sensor desconectado, acumuladas
};

// Zonas del loop() que se marcan como "en curso" justo antes de ejecutar
// cada modulo, para saber donde se quedo colgado el firmware si el
// siguiente arranque es por PANIC/watchdog. Guardado en memoria RTC
// (sobrevive a resets por SW/panic/watchdog, no a un corte de alimentacion).
enum DiagStage : uint8_t {
  DIAG_STAGE_BOOT = 0,
  DIAG_STAGE_LOOP_WIFI,
  DIAG_STAGE_OTA,
  DIAG_STAGE_TEMP_SENSORS,
  DIAG_STAGE_SCHEDULE,
  DIAG_STAGE_DATALOG,
  DIAG_STAGE_WEBSERVER,
  DIAG_STAGE_DIAGLOG,
  DIAG_STAGE_BROADCAST,
  DIAG_STAGE_NTP_TIMEOUT, // reinicio deliberado: WiFi conectado pero la hora
                          // NTP no se sincronizo tras NTP_TIMEOUT_MS (ver .ino)
  DIAG_STAGE_NTP_TIMEOUT_NOWIFI, // reinicio deliberado: sin hora valida tras
                          // NTP_TIMEOUT_MS_ABS desde el arranque, con o sin
                          // WiFi (cubre el caso de quedarse en modo AP)
};

// Marca la zona actual del loop() (ver DiagStage). Muy barato (escribe un
// byte en RTC RAM), llamar antes de cada modulo dentro de loop().
void diaglogSetStage(uint8_t stage);

// Texto legible de una zona (para mostrar el breadcrumb en la web).
const char* diaglogStageText(uint8_t stage);

// Capacidad del buffer LOCAL de emergencia (no del historico completo,
// que ahora vive en el servidor remoto sin limite practico). Ver
// REMOTE_DIAG_FALLBACK_CAPACITY en config.h.
#define DIAG_LOG_CAPACITY_ENTRIES REMOTE_DIAG_FALLBACK_CAPACITY

// Inicializa el modulo: recupera el intervalo de muestreo guardado (NVS,
// pequeño, no relacionado con el historico) y registra un evento
// inmediato con el motivo del ultimo arranque (se encola para enviar al
// servidor remoto en cuanto haya WiFi). Llamar una vez en setup(),
// despues de datalogInit() (comparten el mismo mecanismo de cola/tarea).
void diaglogInit();

// Logica periodica: añade una muestra si ha pasado el intervalo
// configurado, y en ese caso resetea el pico de duracion de loop() para
// medir el siguiente periodo desde cero. Los contadores acumulados
// (wifiReconnects/ntcErrors) se piden a otros modulos por parametro para
// no crear dependencias cruzadas de includes.
//   wsClients      -> web_server.cpp (wsClientCount())
//   wifiReconnects -> wifi_manager.cpp (wifiReconnectCount())
//   ntcErrors      -> temp_sensors.cpp (ntcErrorCount())
// Llamar en cada vuelta del loop() principal.
void diaglogLoop(uint8_t wsClients, uint16_t wifiReconnects, uint16_t ntcErrors);

// Registra la duracion (en microsegundos) de la vuelta de loop() que
// acaba de terminar, actualizando el pico si es mayor que el anterior.
// Reutilizable: no depende de nada del proyecto, solo guarda un maximo.
// Llamar UNA vez por vuelta de loop(), lo antes posible tras medir con
// micros() al principio y al final de loop().
void diaglogRecordLoopDuration(uint32_t micros_duration);

// Construye el JSON de respuesta para el endpoint /api/diag con las
// muestras disponibles. A partir de la migracion, se obtiene por HTTP
// del servidor remoto (o del buffer local de emergencia si no responde).
String diaglogToJson();

// Pide al servidor remoto borrar TODO el historico de diagnostico, y
// limpia tambien el buffer local de emergencia.
void diaglogClear();

// Texto legible del motivo de reset (para mostrar en la web sin tener
// que traducir el codigo numerico en el navegador).
const char* diaglogResetReasonText(uint8_t reason);

// Clasifica la severidad de un arranque para colorear la tabla/graficas
// en la web, sin que el JS tenga que mantener su propia copia de las
// reglas: 0 = arranque normal (encendido), 1 = reinicio deliberado por
// software con motivo conocido (p.ej. timeout de NTP), 2 = reset externo
// o por software sin breadcrumb reconocido (boton RESET, OTA, o un
// ESP.restart() manual), 3 = anomalo (panic, watchdog o brownout: un
// cuelgue o crash real). Se envia ya calculado en el JSON de /api/diag.
uint8_t diaglogEventClass(uint8_t reason, uint8_t breadcrumb);

// ---------------- Intervalo de muestreo ajustable ----------------
// Limites permitidos para el slider de la web (minutos).
#define DIAG_INTERVAL_MIN_MINUTES 1
#define DIAG_INTERVAL_MAX_MINUTES 30

// Devuelve el intervalo de muestreo actual, en milisegundos. Por defecto
// es DIAG_SAMPLE_INTERVAL_MS (config.h), pero puede haberse cambiado en
// caliente desde la web y quedar guardado en NVS.
uint32_t diaglogGetIntervalMs();

// Cambia el intervalo de muestreo (milisegundos), lo satura a los
// limites de arriba y lo persiste en NVS para que sobreviva a reinicios.
void diaglogSetIntervalMs(uint32_t ms);
