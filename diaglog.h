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
 * Guarda una muestra cada DIAG_SAMPLE_INTERVAL_MS y ademas registra un
 * evento inmediato en el arranque (con el motivo del ultimo reset).
 *
 * Almacenamiento: buffer circular en RAM, respaldado en NVS por trozos,
 * igual que datalog.cpp (mismo patron, namespace NVS distinto).
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
};

// Marca la zona actual del loop() (ver DiagStage). Muy barato (escribe un
// byte en RTC RAM), llamar antes de cada modulo dentro de loop().
void diaglogSetStage(uint8_t stage);

// Texto legible de una zona (para mostrar el breadcrumb en la web).
const char* diaglogStageText(uint8_t stage);

// Capacidad total del buffer circular.
#define DIAG_LOG_CAPACITY_ENTRIES DIAG_LOG_CAPACITY

// Inicializa el modulo: carga el buffer guardado en NVS y registra un
// evento inmediato con el motivo del ultimo arranque. Llamar una vez en
// setup(), antes o despues de datalogInit() (son independientes).
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

// Numero de muestras validas actualmente en el buffer.
int diaglogCount();

// Borra todo el historico (RAM + NVS). Deja el buffer como recien
// arrancado (head=count=0); no borra la muestra de arranque actual, asi
// que tras borrar puede volver a crecer con la siguiente diaglogLoop().
void diaglogClear();

// Devuelve la muestra "index" en orden cronologico (0 = la mas antigua).
DiagEntry diaglogGet(int index);

// Construye el JSON de respuesta para el endpoint /api/diag con todas
// las muestras disponibles.
String diaglogToJson();

// Texto legible del motivo de reset (para mostrar en la web sin tener
// que traducir el codigo numerico en el navegador).
const char* diaglogResetReasonText(uint8_t reason);

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
