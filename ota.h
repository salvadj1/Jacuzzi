/*
 * ota.h
 * -----------------------------------------------------------------------
 * Modulo de actualizacion de firmware "Over The Air" (por WiFi), usando
 * la libreria estandar ArduinoOTA. Permite subir nuevo firmware desde el
 * IDE de Arduino / PlatformIO sin cable USB, una vez el ESP32 esta en la
 * misma red local.
 * -----------------------------------------------------------------------
 */
#pragma once

// Inicializa el servicio OTA. Llamar una vez en setup(), despues de que
// el WiFi este conectado (wifiIsConnected() == true).
void setupOta();

// Debe llamarse en cada vuelta del loop() principal mientras haya WiFi
// conectado, para atender las peticiones de actualizacion entrantes.
void loopOta();
