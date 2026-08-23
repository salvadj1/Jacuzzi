/*
 * temp_sensors.h
 * -----------------------------------------------------------------------
 * Modulo de lectura de los 2 sensores NTC10k (analogicos, cada uno en su
 * propio pin). Funciones sueltas y reutilizables en cualquier otro
 * proyecto con sensores NTC.
 *
 * No requiere librerias externas: usa analogRead() y la ecuacion Beta
 * del NTC (ver constantes NTC_* en config.h).
 *
 * Los pines usados (GPIO32/33) son del ADC1, que SI es fiable con el
 * WiFi activo (a diferencia del ADC2, que en la ESP32-CAM comparte
 * hardware con el radio). Aun asi se promedian varias muestras por
 * lectura para suavizar el ruido normal de cualquier lectura analogica.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Configura los pines analogicos. Llamar una vez en setup().
void setupTempSensors();

// Lanza una lectura de ambos sensores si ha pasado SENSOR_READ_MS desde
// la ultima vez. Actualiza g_state.tJacuzzi (T1) y g_state.tSolar (T2).
// Llamar en cada vuelta del loop() principal.
void loopTempSensors();

// Lee un NTC concreto (promediando NTC_ADC_SAMPLES muestras) y devuelve
// la temperatura en grados Celsius. Reutilizable para añadir mas NTC en
// el futuro sin duplicar codigo.
float readNtcCelsius(uint8_t pin);
