/*
 * web_server.h
 * -----------------------------------------------------------------------
 * Modulo del servidor web UNICO de todo el proyecto (puerto 80). Es
 * importante que sea uno solo: tener dos servidores distintos escuchando
 * el mismo puerto (uno para la app y otro para el portal WiFi) provoca
 * conflictos de red y comportamiento erratico (por eso este modulo
 * expone tambien la instancia del servidor, para que wifi_manager.cpp
 * registre en ella sus propias rutas del captive portal en vez de crear
 * su propio servidor).
 *
 * Sirve la app desde PROGMEM y mantiene sincronizados los datos con el
 * navegador mediante WebSocket:
 *   - El ESP32 EMITE su estado (temperaturas, valvulas, programa, etc.)
 *     periodicamente y cuando cambia algo.
 *   - El navegador ENVIA comandos (cambiar modo, guardar programa,
 *     ajustar temperatura objetivo, activar el WiFi de configuracion)
 *     que el ESP32 aplica y guarda de forma persistente.
 *
 * Requiere las librerias "ESPAsyncWebServer" y "AsyncTCP".
 * -----------------------------------------------------------------------
 */
#pragma once
#include <ESPAsyncWebServer.h>

// Crea el servidor, registra las rutas de la app ("/", "/ws") y lo
// arranca. Llamar UNA UNICA VEZ al principio de setup(), antes incluso
// de conectar el WiFi (el servidor no necesita conexion para arrancar).
void webServerBegin();

// Devuelve la instancia unica del servidor para que otros modulos (en
// concreto wifi_manager.cpp) puedan añadir sus propias rutas sin crear
// un segundo servidor.
AsyncWebServer& webServerInstance();

// Envia el estado actual a todos los clientes conectados por WebSocket.
// Llamar periodicamente desde el loop() principal (cada BROADCAST_MS).
void broadcastState();

// Mantenimiento periodico del servidor: limpia clientes WebSocket que se
// desconectaron sin cerrar limpiamente (movil que pierde cobertura, WiFi
// que se duerme, etc.). Sin esto, esas conexiones "zombie" se acumulan y
// terminan agotando memoria/colas internas hasta que el servidor deja de
// responder. Llamar en cada vuelta del loop() principal.
void webServerLoop();

// Numero de clientes WebSocket actualmente conectados (para el registro
// de diagnostico).
uint8_t wsClientCount();
