/*
 * config.h
 * -----------------------------------------------------------------------
 * Definicion centralizada de pines y constantes del proyecto.
 * Placa objetivo: ESP32 DevKit / NodeMCU-32S (30 o 38 pines).
 *
 * Al usar una placa de desarrollo generica (no ESP32-CAM) tenemos mucha
 * mas libertad de pines y sin los conflictos de la camara. Los NTC van
 * en GPIO 32 y 33 porque son ADC1: el ADC1 SI es fiable con el WiFi
 * activo (a diferencia del ADC2, que comparte hardware con el radio y
 * Espressif no garantiza sus lecturas mientras el WiFi esta en marcha).
 *
 * Reparto de pines de este proyecto:
 *   - NTC T1 (jacuzzi)              -> GPIO 32 (ADC1_CH4)
 *   - NTC T2 (solar)                -> GPIO 33 (ADC1_CH5)
 *   - Rele 1: Bomba / Motor         -> GPIO 4
 *   - Rele 2: Ambas valvulas        -> GPIO 17 (una NA y otra NC, en oposicion)
 *   - Rele 4: Libre / futuro uso    -> GPIO 18
 *   - Boton fisico reset WiFi       -> GPIO 0  (boton BOOT de la placa,
 *              se lee solo una vez al arrancar, no interfiere con nada mas)
 *   - LED de estado WiFi            -> GPIO 2  (LED azul on-board en la
 *              mayoria de placas DevKit/NodeMCU-32S)
 *
 * Si tu modulo de reles es de otra polaridad, ajusta RELAY_ACTIVE_LOW.
 * -----------------------------------------------------------------------
 */
#pragma once

// ---------------- Pines de sensores de temperatura (NTC10k analogicos) ----------------
#define PIN_NTC_T1        32   // NTC del jacuzzi (T1) - ADC1
#define PIN_NTC_T2        33   // NTC del serpentin solar (T2) - ADC1

// Parametros del divisor de tension y de la ecuacion Beta del NTC.
// Coinciden con las sondas NTC10k B3950 (impermeables, 2 hilos).
#define NTC_NOMINAL_OHM     10000.0f  // Resistencia del NTC a 25C
#define NTC_NOMINAL_TEMP_C     25.0f
#define NTC_BETA              3950.0f  // Coeficiente Beta de la sonda
#define NTC_SERIES_OHM      10000.0f  // Resistencia fija del divisor de tension
#define NTC_ADC_SAMPLES           16   // Muestras promediadas por lectura (reduce ruido)

// ---------------- Pines del modulo de 4 reles ----------------
#define PIN_RELAY_MOTOR           4   // Rele 1: bomba/motor de circulacion
#define PIN_DE_AMBAS_VALVULAS    17   // Rele 2: mueve las dos valvulas a la vez.
                                       // Una es Normalmente Abierta y la otra
                                       // Normalmente Cerrada, por lo que un unico
                                       // rele las deja siempre en posiciones opuestas.
#define PIN_RELAY_SPARE          18   // Rele 4: reservado para uso futuro

// Polaridad del modulo de reles: la mayoria de modulos "chinos" de reles
// activan el rele cuando el pin se pone en LOW. Cambia a "false" si tu
// modulo es activo en HIGH.
#define RELAY_ACTIVE_LOW  true

// ---------------- Pines auxiliares ----------------
#define PIN_WIFI_RESET_BTN  0  // Boton BOOT de la placa: mantener pulsado al arrancar -> modo config WiFi
#define PIN_STATUS_LED      2  // LED de estado (parpadea buscando WiFi, fijo si conectado)

// ---------------- Identificacion del dispositivo ----------------
#define DEVICE_HOSTNAME     "jacuzzi-esp32"   // Nombre en la red local y para OTA
#define AP_CONFIG_SSID      "Jacuzzi-Config"  // SSID del punto de acceso de configuracion
#define AP_CONFIG_PASSWORD  "12345678"      // Password del punto de acceso de configuracion

// ---------------- Limites de temperatura objetivo ----------------
#define TARGET_TEMP_MIN   20.0f
#define TARGET_TEMP_MAX   40.0f
#define TARGET_TEMP_STEP   0.1f

// ---------------- Offset de calibracion de sensores NTC ----------------
#define OFFSET_TEMP_STEP     0.5f

// ---------------- Limite de descarga solar (T2), solo informativo ----------------
#define SOLAR_DISCHARGE_STEP 0.5f

// ---------------- Tiempos ----------------
#define VALVE_MOVE_MS      10000   // Tiempo que tardan en girar las valvulas motorizadas
#define DISCHARGE_DURATION_MS (5UL * 60UL * 1000UL) // Duracion de la descarga forzada del serpentin solar
#define SENSOR_READ_MS      2000   // Cada cuanto se leen los sensores de temperatura
#define BROADCAST_MS         900   // Cada cuanto se envia el estado por WebSocket
#define AP_CONFIG_WINDOW_MS (5UL * 60UL * 1000UL)  // AP de configuracion abierto 5 min en cada arranque
#define LOG_SAMPLE_INTERVAL_MS (15UL * 60UL * 1000UL) // Cada cuanto se registra una muestra periodica en el historico
