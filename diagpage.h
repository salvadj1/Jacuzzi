/*
 * diagpage.h
 * -----------------------------------------------------------------------
 * Expone el HTML de la pagina "/diag" (registro de diagnostico: heap,
 * WiFi, clientes conectados, motivos de reinicio) como una cadena en
 * PROGMEM, igual que datapage.h hace con la pagina de historico.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

extern const char DIAG_HTML[] PROGMEM;
