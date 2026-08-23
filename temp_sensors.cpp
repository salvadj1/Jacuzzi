/*
 * temp_sensors.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de sensores NTC10k mediante divisor de
 * tension + ecuacion Beta:
 *
 *      3.3V --- NTC_SERIES_OHM --- (nodo leido por el ADC) --- NTC --- GND
 *
 * Si en tu montaje el NTC esta arriba y la resistencia fija abajo,
 * invierte la formula del voltaje en readNtcCelsius() (ver comentario).
 * -----------------------------------------------------------------------
 */
#include "temp_sensors.h"
#include "config.h"
#include "data.h"
#include <math.h>

static unsigned long lastReadMs = 0;

void setupTempSensors() {
  analogSetAttenuation(ADC_11db);      // permite leer hasta ~3.3V en el ADC
  analogReadResolution(12);            // 0-4095
  Serial.printf("[TEMP] Sensores NTC listos (T1=GPIO%d, T2=GPIO%d)\n", PIN_NTC_T1, PIN_NTC_T2);
}

float readNtcCelsius(uint8_t pin) {
  uint32_t sum = 0;
  for (int i = 0; i < NTC_ADC_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200); // pequeño espaciado entre muestras para promediar mejor
  }
  float adcAvg = (float)sum / NTC_ADC_SAMPLES;
  if (adcAvg <= 0) adcAvg = 1; // evita division por cero si el cable esta suelto

  // Voltaje en el nodo intermedio del divisor (escala 0-4095 -> 0-3.3V)
  float vNode = (adcAvg / 4095.0f) * 3.3f;

  // Resistencia del NTC a partir del divisor de tension:
  //   vNode = 3.3 * R_ntc / (R_ntc + R_serie)   =>   R_ntc = R_serie * vNode / (3.3 - vNode)
  // (Si tu NTC esta conectado arriba y la resistencia fija abajo, usa:
  //  R_ntc = R_serie * (3.3 - vNode) / vNode )
  float rNtc = NTC_SERIES_OHM * vNode / (3.3f - vNode);

  // Ecuacion Beta: 1/T = 1/T0 + (1/B) * ln(R/R0)
  float t0Kelvin = NTC_NOMINAL_TEMP_C + 273.15f;
  float invT = (1.0f / t0Kelvin) + (1.0f / NTC_BETA) * log(rNtc / NTC_NOMINAL_OHM);
  float tempKelvin = 1.0f / invT;

  return tempKelvin - 273.15f;
}

void loopTempSensors() {
  if (millis() - lastReadMs < SENSOR_READ_MS) return;
  lastReadMs = millis();

  g_state.tJacuzzi = readNtcCelsius(PIN_NTC_T1) + g_state.offsetT1;
  g_state.tSolar   = readNtcCelsius(PIN_NTC_T2) + g_state.offsetT2;

  Serial.printf("[TEMP] T1=%.1fC  T2=%.1fC\n", g_state.tJacuzzi, g_state.tSolar);
}
