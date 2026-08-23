/*
 * webpage.h
 * -----------------------------------------------------------------------
 * Expone el HTML de la aplicacion como una cadena en PROGMEM (memoria
 * flash), en vez de servirlo desde un sistema de ficheros (LittleFS).
 * Esto evita cualquier problema de montaje/formateo del filesystem: el
 * HTML queda compilado dentro del propio firmware.
 *
 * Si en el futuro quieres volver a editar la web, modifica el contenido
 * de INDEX_HTML en webpage.cpp y vuelve a subir el sketch (no hace falta
 * subir nada aparte, no hay filesystem que gestionar).
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

extern const char INDEX_HTML[] PROGMEM;
