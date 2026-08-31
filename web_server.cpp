/*
 * web_server.cpp
 * -----------------------------------------------------------------------
 * Implementacion del servidor web de la aplicacion. Sirve el HTML desde
 * PROGMEM (webpage.h/.cpp), sin depender de ningun sistema de ficheros.
 * -----------------------------------------------------------------------
 */
#include "web_server.h"
#include "config.h"
#include "data.h"
#include "storage.h"
#include "schedule.h"
#include "webpage.h"
#include "datapage.h"
#include "datalog.h"
#include "diagpage.h"
#include "diaglog.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Construye el JSON con el estado completo del sistema (se envia por
// WebSocket a todos los navegadores conectados)
static String buildStateJson() {
  StaticJsonDocument<512> doc;

  doc["tJacuzzi"]     = g_state.tJacuzzi;
  doc["tSolar"]       = g_state.tSolar;
  doc["offsetT1"]     = g_state.offsetT1;
  doc["offsetT2"]     = g_state.offsetT2;
  doc["pumpOn"]       = g_state.pumpOn;
  doc["autoEnabled"]  = g_state.autoEnabled;
  doc["pumpManual"]   = g_state.pumpManual;
  doc["forceSolar"]   = g_state.forceSolar;
  doc["valvulasActivas"] = g_state.valvulasActivas;
  doc["valvesLocked"] = g_state.valvesLocked;
  doc["targetTemp"]   = g_state.targetTemp;
  doc["solarDischargeTemp"] = g_state.solarDischargeTemp;

  // Descarga forzada del serpentin solar: activa + segundos restantes
  doc["dischargeActive"] = g_state.dischargeActive;
  long dischargeRemainMs = (long)(g_state.dischargeUntil - millis());
  doc["dischargeRemainSec"] = g_state.dischargeActive && dischargeRemainMs > 0
                                 ? (dischargeRemainMs / 1000) : 0;

  time_t now = time(nullptr);
  doc["clock"] = (unsigned long)now;

  time_t next = scheduleNextStart();
  doc["nextStart"] = (unsigned long)next;

  JsonObject sched = doc.createNestedObject("schedule");
  sched["startHour"]   = g_state.schedule.startHour;
  sched["startMinute"] = g_state.schedule.startMinute;
  sched["endHour"]     = g_state.schedule.endHour;
  sched["endMinute"]   = g_state.schedule.endMinute;
  JsonArray days = sched.createNestedArray("days");
  for (int i = 0; i < 7; i++) days.add(g_state.schedule.days[i]);

  String out;
  serializeJson(doc, out);
  return out;
}

// Procesa un comando JSON recibido desde el navegador por WebSocket
static void handleCommand(const String &jsonStr) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
    Serial.println("[WEB] Comando recibido con JSON invalido, se ignora");
    return;
  }

  String cmd = doc["cmd"] | "";
  Serial.printf("[WEB] Comando recibido: %s\n", cmd.c_str());

  if (cmd == "setAuto") {
    bool enabled = doc["enabled"] | false;
    setAutoEnabled(enabled);

  } else if (cmd == "togglePump") {
    setPumpManual(!g_state.pumpManual);

  } else if (cmd == "setForceSolar") {
    bool solar = doc["solar"] | false;
    setForceSolar(solar);

  } else if (cmd == "setTargetTemp") {
    float value = doc["value"] | g_state.targetTemp;
    value = constrain(value, TARGET_TEMP_MIN, TARGET_TEMP_MAX);
    g_state.targetTemp = roundf(value * 10.0f) / 10.0f; // resolucion 0.1
    storageSaveTargetTemp();
    Serial.printf("[WEB] Temperatura objetivo actualizada: %.1f C\n", g_state.targetTemp);

  } else if (cmd == "setTempOffset") {
    int sensor = doc["sensor"] | 1; // 1 = T1 (jacuzzi), 2 = T2 (solar)
    float &offset = (sensor == 2) ? g_state.offsetT2 : g_state.offsetT1;
    float &liveTemp = (sensor == 2) ? g_state.tSolar : g_state.tJacuzzi;
    float value = doc["value"] | offset;
    value = roundf(value / OFFSET_TEMP_STEP) * OFFSET_TEMP_STEP; // resolucion 0.5, sin limite
    float delta = value - offset;
    offset = value;
    liveTemp += delta; // aplica el cambio ya mismo, sin esperar al siguiente muestreo
    storageSaveTempOffsets();
    Serial.printf("[WEB] Offset T%d actualizado: %.1f C\n", sensor, offset);

  } else if (cmd == "setSolarDischargeTemp") {
    float value = doc["value"] | g_state.solarDischargeTemp;
    g_state.solarDischargeTemp = roundf(value / SOLAR_DISCHARGE_STEP) * SOLAR_DISCHARGE_STEP; // resolucion 0.5, solo informativo
    storageSaveSolarDischargeTemp();
    Serial.printf("[WEB] Limite descarga solar actualizado: %.1f C\n", g_state.solarDischargeTemp);

  } else if (cmd == "setSchedule") {
    g_state.schedule.startHour   = doc["startHour"]   | g_state.schedule.startHour;
    g_state.schedule.startMinute = doc["startMinute"] | g_state.schedule.startMinute;
    g_state.schedule.endHour     = doc["endHour"]     | g_state.schedule.endHour;
    g_state.schedule.endMinute   = doc["endMinute"]   | g_state.schedule.endMinute;
    if (doc.containsKey("days")) {
      JsonArray days = doc["days"];
      for (int i = 0; i < 7 && i < (int)days.size(); i++) {
        g_state.schedule.days[i] = days[i];
      }
    }
    storageSaveSchedule();
    Serial.printf("[WEB] Programa guardado: %02d:%02d - %02d:%02d\n",
      g_state.schedule.startHour, g_state.schedule.startMinute,
      g_state.schedule.endHour, g_state.schedule.endMinute);

  } else if (cmd == "setClock") {
    unsigned long epoch = doc["epoch"] | 0;
    if (epoch > 0) {
      struct timeval tv = { (time_t)epoch, 0 };
      settimeofday(&tv, nullptr);
      Serial.println("[WEB] Reloj ajustado manualmente desde la web");
    }

  } else if (cmd == "activateWifiAp") {
    // Activa el captive portal de configuracion WiFi SIN reiniciar el
    // ESP32 (modo dual AP+STA). La gestion completa de redes (añadir,
    // priorizar, eliminar, guardar y salir) se hace en esa propia pagina.
    Serial.println("[WEB] Activacion de captive portal solicitada desde la app");
    wifiActivateConfigAp();

  } else if (cmd == "restart") {
    Serial.println("[WEB] Reinicio solicitado desde la app");
    broadcastState(); // avisa a los clientes antes de reiniciar
    delay(300);       // da tiempo a que el WebSocket envie el mensaje
    ESP.restart();
  }

  broadcastState(); // confirma el cambio a todos los clientes inmediatamente
}

// Callback de eventos del WebSocket (conexion, desconexion, datos)
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WEB] Cliente conectado (#%u), IP %s\n", client->id(), client->remoteIP().toString().c_str());
    client->text(buildStateJson()); // al conectar, le mandamos el estado actual

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WEB] Cliente desconectado (#%u)\n", client->id());

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg((char*)data, len);
      handleCommand(msg);
    }
  }
}

void webServerBegin() {
  Serial.println("[WEB] Iniciando servidor web (unico, puerto 80)...");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // La pagina se sirve directamente desde PROGMEM, no hace falta ningun
  // sistema de ficheros ni subir nada aparte del propio sketch.
  // Si la peticion llega por el punto de acceso de configuracion (en vez
  // de por la red domestica), se sirve el portal de gestion de WiFi en
  // su lugar: comparten servidor y ruta, pero no deben mostrar lo mismo.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (wifiRequestIsFromAp(request) && !wifiIsPermanentApMode()) {
      wifiSendConfigPage(request);
      return;
    }
    request->send_P(200, "text/html", INDEX_HTML);
  });

  // Pagina de graficas del historico de temperaturas/eventos
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", DATA_HTML);
  });

  // API con las muestras del historico (hasta 7 dias), en JSON
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", datalogToJson());
  });

  // Borra un dia completo del historico (boton por dia en "/datos").
  // Parametros de query: from/to = epoch en segundos, rango [from,to).
  server.on("/api/history/deleteday", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("from") || !request->hasParam("to")) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"faltan parametros from/to\"}");
      return;
    }
    uint32_t from = (uint32_t)request->getParam("from")->value().toInt();
    uint32_t to   = (uint32_t)request->getParam("to")->value().toInt();
    datalogDeleteRange(from, to);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Borra TODO el historico (opcion de ultimo recurso "FORMATEAR DATOS"
  // en /datos, para cuando un dia concreto no se puede borrar por datos
  // corruptos).
  server.on("/api/history/format", HTTP_POST, [](AsyncWebServerRequest *request) {
    datalogFormat();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Pagina de diagnostico (heap, WiFi, clientes, motivos de reinicio)
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", DIAG_HTML);
  });

  // API con las muestras de diagnostico, en JSON
  server.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", diaglogToJson());
  });

  // Borra el historico de diagnostico (boton "BORRAR REGISTROS" en /diag)
  server.on("/api/diag/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    diaglogClear();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Cambia el intervalo de muestreo de diagnostico (slider en /diag).
  // Parametro "ms" en la query string, en milisegundos.
  server.on("/api/diag/interval", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ms")) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"falta parametro ms\"}");
      return;
    }
    uint32_t ms = (uint32_t)request->getParam("ms")->value().toInt();
    diaglogSetIntervalMs(ms);
    request->send(200, "application/json", "{\"ok\":true,\"intervalMs\":" + String(diaglogGetIntervalMs()) + "}");
  });

  server.begin();
  Serial.println("[WEB] Servidor listo (accesible por la IP que tenga asignada en cada momento)");
}

AsyncWebServer& webServerInstance() {
  return server;
}

void broadcastState() {
  ws.textAll(buildStateJson());
}

void webServerLoop() {
  // Purga clientes WebSocket muertos (desconexiones sucias). Es la causa
  // mas comun de que el servidor deje de responder tras varias horas
  // funcionando sin que nadie lo reinicie manualmente.
  ws.cleanupClients();
}

uint8_t wsClientCount() {
  return (uint8_t)ws.count();
}