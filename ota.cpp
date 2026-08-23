/*
 * ota.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo OTA.
 * -----------------------------------------------------------------------
 */
#include "ota.h"
#include "config.h"
#include <ArduinoOTA.h>

void setupOta() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Actualizacion iniciada...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] Actualizacion completada.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progreso: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error [%u]\n", error);
  });

  ArduinoOTA.begin();
}

void loopOta() {
  ArduinoOTA.handle();
}
