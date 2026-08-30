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
static uint16_t ntcErrors = 0; // lecturas fuera de rango o con ADC saturado (diagnostico)

// Media movil para suavizar oscilaciones de temperatura mostrada
#define TEMP_AVG_SAMPLES 10
static float t1Buf[TEMP_AVG_SAMPLES] = {0};
static float t2Buf[TEMP_AVG_SAMPLES] = {0};
static uint8_t avgIdx = 0;
static bool avgFilled = false;

static float pushAndAverage(float *buf, float newVal) {
  buf[avgIdx] = newVal;
  uint8_t count = avgFilled ? TEMP_AVG_SAMPLES : (avgIdx + 1);
  float sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += buf[i];
  return sum / count;
}

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

  // ADC pegado a un extremo = sonda desconectada o cortocircuitada; se
  // cuenta como error de diagnostico (el cableado hay que revisarlo),
  // pero se sigue calculando algo razonable para no romper el control.
  bool adcSaturado = (adcAvg <= NTC_ADC_SATURATION_LOW) || (adcAvg >= NTC_ADC_SATURATION_HIGH);
  if (adcAvg <= 0) adcAvg = 1; // evita division por cero si el cable esta suelto

  // Voltaje en el nodo intermedio del divisor (escala 0-4095 -> 0-3.3V)
  float vNode = (adcAvg / 4095.0f) * 3.3f;

  // Resistencia del NTC a partir del divisor de tension:
  // NTC arriba (3.3V->NTC->nodo ADC->R_serie->GND):
  float rNtc = NTC_SERIES_OHM * (3.3f - vNode) / vNode;

  // Ecuacion Beta: 1/T = 1/T0 + (1/B) * ln(R/R0)
  float t0Kelvin = NTC_NOMINAL_TEMP_C + 273.15f;
  float invT = (1.0f / t0Kelvin) + (1.0f / NTC_BETA) * log(rNtc / NTC_NOMINAL_OHM);
  float tempKelvin = 1.0f / invT;
  float tempC = tempKelvin - 273.15f;

  if (adcSaturado || tempC < NTC_TEMP_MIN_C || tempC > NTC_TEMP_MAX_C) {
    if (ntcErrors < 65535) ntcErrors++;
  }

  return tempC;
}

uint16_t ntcErrorCount() {
  return ntcErrors;
}

void loopTempSensors() {
  if (millis() - lastReadMs < SENSOR_READ_MS) return;
  lastReadMs = millis();

  float rawT1 = readNtcCelsius(PIN_NTC_T1) + g_state.offsetT1;
  float rawT2 = readNtcCelsius(PIN_NTC_T2) + g_state.offsetT2;

  g_state.tJacuzzi = pushAndAverage(t1Buf, rawT1);
  g_state.tSolar   = pushAndAverage(t2Buf, rawT2);

  avgIdx = (avgIdx + 1) % TEMP_AVG_SAMPLES;
  if (avgIdx == 0) avgFilled = true;

  Serial.printf("[TEMP] T1=%.1fC  T2=%.1fC\n", g_state.tJacuzzi, g_state.tSolar);
}