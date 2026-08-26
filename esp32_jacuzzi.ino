/*
 * esp32_jacuzzi.ino
 * -----------------------------------------------------------------------
 * Control termico y de filtracion del jacuzzi - ESP32 DevKit / NodeMCU-32S.
 *
 * Este archivo NO contiene logica propia: unicamente inicializa y llama
 * a las funciones de cada modulo. Toda la logica esta repartida en:
 *
 *   config.h          Pines y constantes
 *   data.h            Estado global compartido (g_state)
 *   storage.*         Guardado persistente (WiFi, programa, temperatura)
 *   wifi_manager.*     Conexion WiFi (redes conocidas + portal de config)
 *   relays.*          Control de bomba y valvulas
 *   temp_sensors.*    Lectura de los 2 sensores NTC10k
 *   schedule.*        Programa horario + logica de modo automatico
 *   ota.*             Actualizacion de firmware por WiFi
 *   web_server.*      Servidor web + WebSocket con el navegador
 *   webpage.*         HTML de la app embebido en PROGMEM
 *
 * Todos los mensajes de estado del sistema se imprimen por el Monitor
 * Serie (115200 baudios) con un prefijo [MODULO] para identificar de
 * donde viene cada mensaje.
 * -----------------------------------------------------------------------
 */
#include "config.h"
#include "data.h"
#include "storage.h"
#include "wifi_manager.h"
#include "relays.h"
#include "temp_sensors.h"
#include "schedule.h"
#include "datalog.h"
#include "ota.h"
#include "web_server.h"
#include <WiFi.h>

// Estado global unico del sistema (declarado como "extern" en data.h)
SystemState g_state;

static bool ntpConfigured = false;
static bool otaStarted = false;
static unsigned long lastBroadcast = 0;

void setup() {
  Serial.begin(115200);
  delay(300); // margen para que el monitor serie enganche los primeros mensajes
  Serial.println();
  Serial.println("========================================");
  Serial.println("  CONTROL JACUZZI ESP32 - Arrancando...");
  Serial.println("========================================");

  Serial.println("[MAIN] Inicializando almacenamiento persistente (NVS)...");
  storageInit();
  storageLoadSchedule();
  storageLoadTargetTemp();
  storageLoadTempOffsets();
  storageLoadSolarDischargeTemp();
  Serial.printf("[MAIN] Programa cargado: %02d:%02d - %02d:%02d\n",
    g_state.schedule.startHour, g_state.schedule.startMinute,
    g_state.schedule.endHour, g_state.schedule.endMinute);
  Serial.printf("[MAIN] Temperatura objetivo cargada: %.1f C\n", g_state.targetTemp);

  Serial.println("[MAIN] Inicializando reles...");
  setupRelays();

  Serial.println("[MAIN] Inicializando sensores de temperatura (NTC)...");
  setupTempSensors();

  Serial.println("[MAIN] Inicializando logica de programacion horaria...");
  setupSchedule();

  Serial.println("[MAIN] Inicializando registro historico (datalog)...");
  datalogInit();

  // El modo WiFi debe activarse ANTES de arrancar el servidor web: el
  // servidor (AsyncWebServer/AsyncTCP) necesita que la pila de red ya
  // este inicializada, o el ESP32 crashea en el arranque. Por eso aqui
  // solo se fija el modo; la conexion en si (setupWifi) se hace despues.
  WiFi.mode(WIFI_AP_STA);

  // El servidor web se arranca UNA SOLA VEZ y es el UNICO servidor de
  // todo el proyecto (tanto para la app como para el captive portal),
  // asi que debe estar listo antes de que wifi_manager registre en el
  // sus rutas de configuracion.
  webServerBegin();

  Serial.println("[MAIN] Inicializando WiFi...");
  setupWifi(); // conecta a la mejor red conocida, y abre el AP de config

  Serial.println("[MAIN] Setup completado, entrando en loop principal.");
}

void loop() {
  loopWifi();

  // OTA y la sincronizacion horaria (NTP) solo tienen sentido una vez
  // estamos conectados como cliente a una red real.
  if (wifiIsConnected()) {
    if (!ntpConfigured) {
      Serial.println("[MAIN] WiFi conectado. Sincronizando hora por NTP...");
      configTime(3600, 3600, "pool.ntp.org", "time.nist.gov"); // UTC+1 con horario de verano; ajustar segun pais
      ntpConfigured = true;
    }
    if (!otaStarted) {
      Serial.println("[MAIN] Habilitando actualizacion OTA...");
      setupOta();
      otaStarted = true;
      Serial.println("[MAIN] OTA lista. El dispositivo es visible en la red como jacuzzi-esp32.");
    }
    loopOta();
  }

  loopTempSensors();
  loopSchedule();
  datalogLoop();

  if (millis() - lastBroadcast > BROADCAST_MS) {
    lastBroadcast = millis();
    broadcastState();
  }
}
