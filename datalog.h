/*
 * datalog.h
 * -----------------------------------------------------------------------
 * Modulo de registro historico para la grafica de "DATOS".
 *
 * Guarda una muestra cada LOG_SAMPLE_INTERVAL_MS (temperaturas + estado)
 * y ademas registra al instante cualquier cambio de estado (bomba, modo
 * auto, valvulas, forzado manual), para no perder esos eventos entre
 * dos muestras periodicas.
 *
 * Almacenamiento: buffer circular en RAM, respaldado en NVS (Preferences)
 * en varios trozos (chunks) para no superar el limite de tamaño de una
 * entrada NVS. Con muestras cada 15 min, la capacidad configurada cubre
 * 7 dias completos de sobra, incluso contando eventos extra.
 *
 * Reutilizable: este modulo no depende de nada especifico del jacuzzi
 * salvo de "leer" temperaturas/estado desde g_state; para otro proyecto
 * bastaria con adaptar datalogSampleNow() a los datos que se quieran
 * registrar.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Bits del campo "flags" de cada muestra
#define LOG_FLAG_PUMP        (1 << 0) // Bomba en marcha
#define LOG_FLAG_AUTO        (1 << 1) // Modo automatico activado
#define LOG_FLAG_VALVES      (1 << 2) // Valvulas desviadas a solar (true) / filtro (false)
#define LOG_FLAG_FORCE_SOLAR (1 << 3) // Selector manual en posicion solar

// Una muestra del historico (9 bytes, sin padding gracias a "packed")
struct __attribute__((packed)) LogEntry {
  uint32_t timestamp;   // Epoch (segundos), 0 = entrada vacia/no usada
  int16_t  tJacuzziX10; // Temperatura T1 x10 (1 decimal), ej. 314 = 31.4 C
  int16_t  tSolarX10;   // Temperatura T2 x10
  uint8_t  flags;       // Combinacion de LOG_FLAG_*
};

// Capacidad total del buffer circular. A 15 min/muestra cubre ~10.4 dias
// solo con muestreo periodico, dejando margen para las muestras extra
// que se añaden en cada evento (se van descartando las mas antiguas).
#define LOG_CAPACITY 1000

// Inicializa el modulo: carga el buffer guardado en NVS (si existe).
// Llamar una vez en setup(), despues de storageInit().
void datalogInit();

// Logica periodica: comprueba si toca muestra por tiempo (15 min) o si
// algun estado ha cambiado desde la ultima muestra registrada, y en tal
// caso añade una nueva entrada. Llamar en cada vuelta del loop().
void datalogLoop();

// Numero de muestras validas actualmente en el buffer (<= LOG_CAPACITY).
int datalogCount();

// Devuelve la muestra "index" en orden cronologico (0 = la mas antigua
// disponible, datalogCount()-1 = la mas reciente).
LogEntry datalogGet(int index);

// Construye el JSON de respuesta para el endpoint /api/history con todas
// las muestras disponibles (hasta 7 dias). Pensado para escribirse
// directamente en la respuesta HTTP (evita construir una String gigante
// en memoria).
String datalogToJson();

// Borra todas las muestras cuyo timestamp cae en [fromTs, toTs) (pensado
// para borrar un dia completo desde la web, boton por dia en "/datos") y
// compacta el resto. Operacion puntual bajo demanda -no se llama desde
// el loop()-, reescribe el historico completo en NVS.
void datalogDeleteRange(uint32_t fromTs, uint32_t toTs);
