/*
 * datalog.h
 * -----------------------------------------------------------------------
 * Modulo de registro historico para la grafica de "DATOS".
 *
 * MIGRACION: este modulo ya NO guarda el historico en el propio ESP32
 * (antes: buffer circular en RAM + respaldo en NVS por chunks). Ahora
 * actua como CLIENTE de un servidor Python (FastAPI + SQLite) que corre
 * 24/7 en un PC de la misma red local (ver jacuzzi_server/ en la raiz del
 * repo y REMOTE_LOG_* en config.h).
 *
 * - addEntry() ya no escribe en NVS: encola la muestra y una tarea aparte
 *   la envia por HTTP POST a {REMOTE_LOG_SERVER_URL}/api/data, sin
 *   bloquear el loop() principal (control de reles/sensores).
 * - datalogToJson() hace un HTTP GET a {REMOTE_LOG_SERVER_URL}/api/history
 *   y devuelve la respuesta tal cual (mismo formato JSON que antes, asi
 *   que la pagina "/datos" no necesita cambios).
 * - datalogDeleteRange()/datalogFormat() reenvian la orden al servidor.
 * - Si el servidor no responde (PC apagado, red caida), las muestras no
 *   enviadas se guardan en un pequeño buffer de emergencia en RAM
 *   (REMOTE_LOG_FALLBACK_CAPACITY, ver datalog.cpp) y se reintentan mas
 *   tarde; las consultas ese rato se sirven desde ese mismo buffer.
 *
 * Sigue guardando una muestra cada LOG_SAMPLE_INTERVAL_MS (temperaturas +
 * estado) y ademas registra al instante cualquier cambio de estado
 * (bomba, modo auto, valvulas, forzado manual), igual que antes.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Bits del campo "flags" de cada muestra
#define LOG_FLAG_PUMP        (1 << 0) // Bomba en marcha
#define LOG_FLAG_AUTO        (1 << 1) // Modo automatico activado
#define LOG_FLAG_VALVES      (1 << 2) // Valvulas desviadas a solar (true) / filtro (false)
#define LOG_FLAG_FORCE_SOLAR (1 << 3) // Selector manual en posicion solar

// Una muestra del historico (9 bytes, sin padding gracias a "packed").
// Mismo formato que usa el servidor Python (ver jacuzzi_server/main.py) y
// el JSON de /api/history, para no tener que traducir nada por el camino.
struct __attribute__((packed)) LogEntry {
  uint32_t timestamp;   // Epoch (segundos), 0 = entrada vacia/no usada
  int16_t  tJacuzziX10; // Temperatura T1 x10 (1 decimal), ej. 314 = 31.4 C
  int16_t  tSolarX10;   // Temperatura T2 x10
  uint8_t  flags;       // Combinacion de LOG_FLAG_*
};

// Inicializa el modulo: prepara la cola/tarea de envio HTTP en segundo
// plano. Llamar una vez en setup(), despues de storageInit().
void datalogInit();

// Logica periodica: comprueba si toca muestra por tiempo (15 min) o si
// algun estado ha cambiado desde la ultima muestra registrada, y en tal
// caso encola una nueva entrada para enviar al servidor. No bloquea.
// Llamar en cada vuelta del loop().
void datalogLoop();

// Construye (via HTTP GET al servidor, con fallback local si no responde)
// el JSON de respuesta para el endpoint /api/history con las muestras
// disponibles. Formato: {"samples":[[ts,t1,t2,flags], ...]}.
String datalogToJson();

// Pide al servidor borrar las muestras cuyo timestamp cae en
// [fromTs, toTs) (boton "borrar dia" en la web).
void datalogDeleteRange(uint32_t fromTs, uint32_t toTs);

// Pide al servidor borrar TODO el historico y limpia el buffer local de
// emergencia. Opcion de ultimo recurso ("FORMATEAR" en /diag y /datos).
void datalogFormat();
