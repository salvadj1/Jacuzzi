/*
 * storage.h
 * -----------------------------------------------------------------------
 * Modulo de almacenamiento persistente usando la memoria NVS del ESP32
 * (libreria Preferences). Guarda:
 *   - Lista de redes WiFi conocidas (SSID + password).
 *   - Programa de filtracion/calentamiento configurado desde la web.
 *   - Temperatura objetivo elegida por el usuario.
 *
 * Todos los metodos son funciones sueltas y reutilizables, sin clases.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>
#include "data.h"

#define MAX_KNOWN_NETWORKS 5

// Una red WiFi conocida guardada por el usuario
struct KnownNetwork {
  String ssid;
  String password;
};

// Inicializa el almacenamiento persistente. Llamar una vez en setup().
void storageInit();

// ---------------- Redes WiFi conocidas ----------------
// Devuelve el numero de redes conocidas guardadas
int storageGetKnownNetworksCount();

// Rellena "out" con la red conocida en la posicion "index" (0..count-1)
KnownNetwork storageGetKnownNetwork(int index);

// Añade (o actualiza si el SSID ya existia) una red conocida a la lista.
// Las redes se guardan en orden: el indice 0 es la de MAYOR prioridad.
void storageSaveKnownNetwork(const String &ssid, const String &password);

// Elimina la red conocida en la posicion "index", desplazando el resto
void storageDeleteKnownNetwork(int index);

// Sube o baja la prioridad de una red intercambiandola con la contigua.
// delta = -1 sube prioridad (se conecta antes), delta = +1 la baja.
void storageMoveKnownNetwork(int index, int delta);

// Elimina todas las redes conocidas guardadas
void storageClearKnownNetworks();

// ---------------- Modo de conexion WiFi ----------------
// 0 = usar redes wifi disponibles (AP temporal de 5 min, se cierra al
//     conectar a una red conocida). 1 = AP permanente (nunca se cierra,
//     no se intenta conectar a ninguna red domestica).
void storageLoadWifiMode(int &mode);
void storageSaveWifiMode(int mode);

// ---------------- Programa de filtracion ----------------
// Carga el programa guardado en g_state.schedule
void storageLoadSchedule();

// Guarda el programa actual de g_state.schedule en memoria persistente
void storageSaveSchedule();

// ---------------- Temperatura objetivo ----------------
// Carga la temperatura objetivo guardada en g_state.targetTemp
void storageLoadTargetTemp();

// Guarda la temperatura objetivo actual de g_state.targetTemp
void storageSaveTargetTemp();

// ---------------- Limite de descarga solar ----------------
// Carga el limite guardado en g_state.solarDischargeTemp
void storageLoadSolarDischargeTemp();

// Guarda el limite actual de g_state.solarDischargeTemp
void storageSaveSolarDischargeTemp();

// ---------------- Offset de calibracion de sensores ----------------
// Carga los offsets guardados en g_state.offsetT1 / g_state.offsetT2
void storageLoadTempOffsets();

// Guarda los offsets actuales de g_state.offsetT1 / g_state.offsetT2
void storageSaveTempOffsets();

// ---------------- Estado del modo automatico ----------------
// Persiste si el modo automatico estaba activado, para que al reiniciarse
// el ESP32 (por corte de luz, watchdog, etc.) vuelva a quedar exactamente
// como estaba, sin que el usuario tenga que volver a activarlo a mano.
void storageLoadAutoEnabled();

// Guarda el estado actual de g_state.autoEnabled
void storageSaveAutoEnabled();
