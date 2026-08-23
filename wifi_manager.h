/*
 * wifi_manager.h
 * -----------------------------------------------------------------------
 * Modulo de gestion de WiFi:
 *   - En CADA arranque, ademas de intentar conectar a la red conocida de
 *     mayor prioridad que este visible, abre TAMBIEN un punto de acceso
 *     propio ("AP_CONFIG_SSID") durante AP_CONFIG_WINDOW_MS (5 minutos)
 *     como red de seguridad. Se cierra solo si logra conectar antes.
 *   - El punto de acceso tambien se puede activar BAJO DEMANDA en
 *     cualquier momento (por ejemplo, al pulsar un boton en la app web)
 *     sin necesidad de reiniciar el ESP32, gracias al modo dual AP+STA.
 *   - El punto de acceso es un CAPTIVE PORTAL: al conectarse a el, el
 *     movil/PC detecta automaticamente que hay que "iniciar sesion en la
 *     red" y abre la pagina de gestion de WiFi solo, sin que el usuario
 *     tenga que teclear ninguna direccion.
 *   - La pagina de gestion permite: escanear y añadir redes nuevas,
 *     subir/bajar su prioridad de conexion, eliminarlas, y un boton
 *     "Guardar y salir" que reinicia el ESP32 para aplicar los cambios.
 *   - Expone setupWifi()/loopWifi() para que el .ino solo tenga que
 *     llamarlas, sin conocer los detalles internos.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Inicializa el subsistema WiFi. Debe llamarse una vez en setup().
// Arranca el punto de acceso de configuracion (ventana de 5 min) y en
// paralelo intenta conectar a la red conocida de mayor prioridad visible.
void setupWifi();

// Debe llamarse en cada vuelta del loop() principal. Gestiona:
//   - el captive portal mientras el AP este activo,
//   - el cierre automatico del AP al cumplirse la ventana de tiempo,
//   - la reconexion automatica si se pierde la señal STA.
void loopWifi();

// Activa el punto de acceso de configuracion BAJO DEMANDA (sin reiniciar
// el ESP32), por ejemplo cuando el usuario pulsa "Configurar WiFi" en la
// app web estando ya conectado. Si ya estuviera activo, no hace nada.
void wifiActivateConfigAp();

// Indica si el ESP32 esta conectado a una red WiFi como cliente (STA)
bool wifiIsConnected();

// Indica si el punto de acceso de configuracion esta activo ahora mismo
bool wifiIsApActive();

// Distingue por que interfaz de red llego una peticion HTTP: true si
// entro por el punto de acceso de configuracion (192.168.4.x), false si
// entro por la red domestica (STA). Es necesario porque ambas comparten
// el mismo servidor web y la misma ruta "/": sin esto, entrar a
// 192.168.4.1 mostraria la app normal en vez del portal de configuracion.
bool wifiRequestIsFromAp(AsyncWebServerRequest *request);

// Envia la pagina de gestion de WiFi (captive portal) como respuesta a
// la peticion dada. Solo debe llamarse cuando wifiRequestIsFromAp() es true.
void wifiSendConfigPage(AsyncWebServerRequest *request);
