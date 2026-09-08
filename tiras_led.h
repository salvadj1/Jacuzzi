/*
 * tiras_led.h
 * -----------------------------------------------------------------------
 * Modulo de control de tiras de LED RGB genericas por Bluetooth Low
 * Energy (las tipicas "smart LED strip" de AliExpress, controladas de
 * fabrica por apps como "Happy Lighting" / "LED BLE" mediante un
 * caracteristica BLE que acepta comandos de 9 bytes).
 *
 * Responsabilidades de este modulo (TODO autocontenido aqui dentro):
 *   - Cliente BLE (NimBLE-Arduino) hacia una o varias tiras.
 *   - Escaneo BLE bajo demanda (no permanente) para descubrir tiras.
 *   - Grupo de tiras emparejadas (por direccion MAC), con reconexion
 *     automatica no bloqueante y backoff (mismo patron que wifi_manager).
 *   - Color RGB, brillo, y 15 efectos con velocidad ajustable, calculados
 *     en el ESP32 y enviados a la tira como comandos de color/brillo.
 *   - Sistema de programas (dias de la semana + hora ON + hora OFF).
 *   - Pagina web propia ("/leds") con el mismo estilo visual que la app
 *     principal, y su API JSON (GET/POST) para controlar todo lo anterior.
 *   - Persistencia en NVS (Preferences) de tiras emparejadas, color,
 *     brillo, efecto, velocidad y programas: se recarga sola al arrancar.
 *
 * Requiere la libreria "NimBLE-Arduino" (cliente BLE ligero, mucho menos
 * RAM/flash que la libreria BLE "clasica" de Espressif).
 *
 * Integracion con el resto del proyecto: SOLO se llama desde el .ino.
 * Ningun otro archivo del proyecto necesita tocarse aparte del boton ya
 * anadido en webpage.cpp.
 * -----------------------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

// Inicializa el modulo: carga de NVS el grupo de tiras, color/brillo/
// efecto/velocidad y los programas guardados, e inicializa la pila BLE
// (NimBLE) en modo cliente. Registra ademas las rutas web ("/leds" y su
// API) en el servidor unico del proyecto.
// Llamar UNA VEZ en setup(), despues de webServerBegin().
void ledsInit();

// Logica no bloqueante a llamar en cada vuelta del loop() principal:
//   - Avanza el efecto activo (si hay uno) segun su velocidad.
//   - Gestiona la reconexion BLE con backoff a las tiras del grupo.
//   - Aplica el programa horario (enciende/apaga segun dias/horas).
//   - Gestiona el escaneo BLE bajo demanda (si esta en curso).
void ledsLoop();
