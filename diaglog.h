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

// Una muestra de diagnostico (16 bytes, sin padding gracias a "packed")
struct __attribute__((packed)) DiagEntry {
  uint32_t timestamp;     // Epoch (segundos), 0 = aun sin hora sincronizada
  uint32_t freeHeap;      // Heap libre en el momento de la muestra (bytes)
  uint32_t minFreeHeap;   // Heap libre minimo historico desde el arranque (bytes)
  uint32_t uptimeSec;     // Segundos desde el ultimo arranque
  int8_t   rssi;          // Nivel de señal WiFi (dBm), 0 si no aplica
  uint8_t  wsClients;     // Clientes WebSocket conectados en ese momento
  uint8_t  wifiConnected; // 1 = conectado a red domestica, 0 = no
  uint8_t  resetReason;   // Motivo de arranque (esp_reset_reason_t), solo valido en la muestra de arranque
};

// Capacidad total del buffer circular.
#define DIAG_LOG_CAPACITY_ENTRIES DIAG_LOG_CAPACITY

// Inicializa el modulo: carga el buffer guardado en NVS y registra un
// evento inmediato con el motivo del ultimo arranque. Llamar una vez en
// setup(), antes o despues de datalogInit() (son independientes).
void diaglogInit();

// Logica periodica: añade una muestra si ha pasado el intervalo
// configurado. "wsClients" se pasa desde fuera porque el conteo de
// clientes WebSocket vive en web_server.cpp. Llamar en cada vuelta del
// loop() principal.
void diaglogLoop(uint8_t wsClients);

// Numero de muestras validas actualmente en el buffer.
int diaglogCount();

// Devuelve la muestra "index" en orden cronologico (0 = la mas antigua).
DiagEntry diaglogGet(int index);

// Construye el JSON de respuesta para el endpoint /api/diag con todas
// las muestras disponibles.
String diaglogToJson();

// Texto legible del motivo de reset (para mostrar en la web sin tener
// que traducir el codigo numerico en el navegador).
const char* diaglogResetReasonText(uint8_t reason);
