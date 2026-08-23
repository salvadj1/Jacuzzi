/*
 * wifi_manager.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo WiFi: modo dual AP+STA, seleccion de red por
 * PRIORIDAD (orden guardado, no por señal), y captive portal con gestion
 * completa de redes conocidas (añadir, priorizar, eliminar).
 *
 * IMPORTANTE: las rutas del captive portal se registran en el MISMO
 * servidor que usa la app (ver web_server.h/.cpp), nunca en un servidor
 * propio. Tener dos servidores distintos escuchando el puerto 80 a la
 * vez provoca conflictos de red (por eso antes, al abrir la app estando
 * ya conectado, se acababa viendo la pagina de configuracion WiFi: el
 * servidor equivocado respondia a la peticion).
 * -----------------------------------------------------------------------
 */
#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include "web_server.h"
#include <WiFi.h>
#include <DNSServer.h>

static DNSServer dnsServer;
static bool apActive = false;
static bool routesRegistered = false; // las rutas solo se registran una vez, aunque el AP se active varias veces
static unsigned long apOpenedAt = 0;
static unsigned long lastReconnectAttempt = 0;

// Modo de conexion elegido por el usuario (persistido en NVS):
// 0 = usar redes wifi disponibles, 1 = AP permanente. Ver storage.h.
#define WIFI_MODE_AUTO_NETWORKS 0
#define WIFI_MODE_AP_PERMANENT  1
static int wifiMode = WIFI_MODE_AUTO_NETWORKS;

// Pagina de gestion de WiFi (captive portal). Permite escanear y añadir
// redes, subir/bajar su prioridad, eliminarlas, y guardar+reiniciar.
static const char CONFIG_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="es"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Configurar WiFi - Jacuzzi ESP32</title>
<style>
body{font-family:sans-serif;background:#0b1210;color:#d8e2dd;padding:20px;max-width:480px;margin:0 auto;}
h2{color:#e8a33d;font-size:19px;} h3{color:#e8a33d;font-size:15px;margin-top:26px;}
input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box;background:#16211c;color:#d8e2dd;border:1px solid #22332c;border-radius:4px;}
button{padding:9px 14px;background:#4fd67a;color:#0b1210;border:none;border-radius:4px;font-weight:bold;cursor:pointer;margin-top:6px;}
button.secundario{background:#16211c;color:#d8e2dd;border:1px solid #22332c;}
button.peligro{background:#e2513f;color:#fff;}
.red-scan{padding:7px;border:1px solid #22332c;margin:4px 0;border-radius:4px;cursor:pointer;font-size:13px;}
.conocida{display:flex;align-items:center;gap:6px;padding:8px;border:1px solid #22332c;border-radius:4px;margin:6px 0;font-size:13px;}
.conocida .nombre{flex:1;}
.conocida .prio{color:#6f8177;font-size:11px;width:18px;}
.conocida button{padding:4px 8px;margin:0;font-size:12px;}
.salir{width:100%;margin-top:24px;padding:12px;font-size:14px;}
.modo-wifi{display:flex;gap:8px;margin-bottom:4px;}
.modo-wifi button{flex:1;margin:0;background:#16211c;color:#d8e2dd;border:1px solid #22332c;font-weight:normal;}
.modo-wifi button.active{border-color:#4fd67a;color:#4fd67a;}
#modoHint{font-size:11px;color:#6f8177;margin:6px 0 18px;}
</style></head><body>
<h2>Configuracion WiFi - Jacuzzi ESP32</h2>

<div class="modo-wifi">
  <button id="btnModoAuto">USAR REDES WIFI DISPONIBLES</button>
  <button id="btnModoAP">USAR CONEXION AP PERMANENTE</button>
</div>
<p id="modoHint">Comprobando modo actual...</p>

<div id="estadoConexion" style="font-size:12px;color:#6f8177;margin-bottom:10px;">Comprobando conexion actual...</div>

<h3>Redes conocidas (por orden de prioridad)</h3>
<div id="conocidas">Cargando...</div>

<h3>Añadir red nueva</h3>
<p style="font-size:12px;color:#6f8177;">Redes detectadas (toca una para rellenar el SSID):</p>
<div id="scan">Buscando...</div>
<input type="text" id="ssid" placeholder="Nombre de la red (SSID)">
<input type="password" id="password" placeholder="Contraseña">
<button id="btnAdd">AÑADIR A LA LISTA</button>

<button class="salir" id="btnSalir">GUARDAR Y SALIR (reinicia el ESP32)</button>

<script>
function marcarModo(mode){
  document.getElementById('btnModoAuto').classList.toggle('active', mode==0);
  document.getElementById('btnModoAP').classList.toggle('active', mode==1);
  document.getElementById('modoHint').textContent = mode==1
    ? 'El punto de acceso permanecera siempre activo, sin intentar conectar a ninguna red domestica.'
    : 'Se intentara conectar a tus redes guardadas; el punto de acceso se cerrara al conseguirlo.';
}

function cargarEstado(){
  fetch('/api/estado').then(r=>r.json()).then(e=>{
    const cont = document.getElementById('estadoConexion');
    cont.textContent = e.connected
      ? `Conectado actualmente a "${e.ssid}" · IP: ${e.ip}`
      : 'Sin conexion a ninguna red domestica en este momento';
    marcarModo(e.wifiMode);
  });
}

document.getElementById('btnModoAuto').onclick = ()=>{
  fetch('/modo-wifi', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'mode=0' })
    .then(()=> marcarModo(0));
};
document.getElementById('btnModoAP').onclick = ()=>{
  fetch('/modo-wifi', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'mode=1' })
    .then(()=> marcarModo(1));
};

function cargarConocidas(){
  fetch('/api/networks').then(r=>r.json()).then(list=>{
    const cont = document.getElementById('conocidas');
    cont.innerHTML = list.length ? '' : '<p style="font-size:12px;color:#6f8177;">Ninguna todavia.</p>';
    list.forEach((n, i)=>{
      const d = document.createElement('div');
      d.className = 'conocida';
      d.innerHTML = `<span class="prio">${i+1}</span><span class="nombre">${n.ssid}</span>`;
      const up = document.createElement('button');
      up.textContent = '▲'; up.className='secundario';
      up.onclick = ()=> fetch('/mover?index='+i+'&delta=-1').then(cargarConocidas);
      const down = document.createElement('button');
      down.textContent = '▼'; down.className='secundario';
      down.onclick = ()=> fetch('/mover?index='+i+'&delta=1').then(cargarConocidas);
      const del = document.createElement('button');
      del.textContent = '🗑'; del.className='peligro';
      del.onclick = ()=> fetch('/eliminar?index='+i).then(cargarConocidas);
      d.appendChild(up); d.appendChild(down); d.appendChild(del);
      cont.appendChild(d);
    });
  });
}

function cargarScan(){
  fetch('/scan-wifi').then(r=>r.json()).then(list=>{
    const cont = document.getElementById('scan');
    cont.innerHTML = '';
    list.forEach(r=>{
      const d = document.createElement('div');
      d.className='red-scan'; d.textContent = r.ssid + ' (' + r.rssi + ' dBm)';
      d.onclick = ()=>{ document.getElementById('ssid').value = r.ssid; };
      cont.appendChild(d);
    });
  });
}

document.getElementById('btnAdd').onclick = ()=>{
  const ssid = document.getElementById('ssid').value.trim();
  const password = document.getElementById('password').value;
  if(!ssid) return;
  fetch('/guardar-wifi', {
    method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)
  }).then(()=>{
    document.getElementById('ssid').value=''; document.getElementById('password').value='';
    cargarConocidas();
  });
};

document.getElementById('btnSalir').onclick = ()=>{
  document.body.innerHTML = '<h2>Guardando y reiniciando...</h2><p>El ESP32 se reiniciara y se conectara con la nueva configuracion. Puedes cerrar esta pagina.</p>';
  fetch('/salir', { method:'POST' });
};

cargarEstado();
cargarConocidas();
cargarScan();
</script>
</body></html>
)HTML";

// ---------------- Utilidades internas ----------------

// Devuelve la red conocida de MAYOR prioridad que este visible ahora
// mismo (recorre la lista guardada en orden; la primera que aparezca en
// el escaneo gana, independientemente de su señal RSSI).
static int findBestKnownNetwork(String &outSsid, String &outPass) {
  Serial.println("[WIFI] Escaneando redes disponibles...");
  int found = WiFi.scanNetworks();
  Serial.printf("[WIFI] %d redes visibles\n", found);

  int knownCount = storageGetKnownNetworksCount();
  Serial.printf("[WIFI] %d redes conocidas guardadas (por prioridad)\n", knownCount);

  for (int i = 0; i < knownCount; i++) {
    KnownNetwork known = storageGetKnownNetwork(i);
    if (known.ssid.length() == 0) continue;

    for (int j = 0; j < found; j++) {
      if (WiFi.SSID(j) == known.ssid) {
        outSsid = known.ssid;
        outPass = known.password;
        Serial.printf("[WIFI] Red de mayor prioridad visible: %s (prioridad #%d)\n", outSsid.c_str(), i + 1);
        return i;
      }
    }
  }

  Serial.println("[WIFI] Ninguna red conocida esta visible ahora mismo");
  return -1;
}

static bool tryConnect(const String &ssid, const String &password, unsigned long timeoutMs) {
  Serial.printf("[WIFI] Conectando a \"%s\"...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  bool ok = WiFi.status() == WL_CONNECTED;
  if (ok) {
    Serial.printf("[WIFI] Conectado. IP asignada: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] No se pudo conectar (timeout)");
  }
  return ok;
}

// Construye el JSON con la lista de redes conocidas (sin passwords)
static String buildKnownNetworksJson() {
  int count = storageGetKnownNetworksCount();
  String json = "[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    KnownNetwork n = storageGetKnownNetwork(i);
    json += "{\"ssid\":\"" + n.ssid + "\"}";
  }
  json += "]";
  return json;
}

// Registra las rutas del captive portal en el servidor COMPARTIDO de la
// app (nunca en un servidor propio). Cada ruta comprueba "apActive" por
// seguridad: si alguien las llama estando el AP cerrado (por ejemplo,
// un dispositivo de la red domestica que las adivina), se rechazan.
static void registerCaptiveRoutes() {
  AsyncWebServer &server = webServerInstance();

  server.on("/api/estado", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool connected = (WiFi.status() == WL_CONNECTED);
    String json = "{\"connected\":" + String(connected ? "true" : "false") +
                  ",\"ssid\":\"" + (connected ? WiFi.SSID() : "") + "\"" +
                  ",\"ip\":\"" + (connected ? WiFi.localIP().toString() : "") + "\"" +
                  ",\"wifiMode\":" + String(wifiMode) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/modo-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    int mode = request->arg("mode").toInt();
    if (mode != WIFI_MODE_AUTO_NETWORKS && mode != WIFI_MODE_AP_PERMANENT) { request->send(400); return; }
    wifiMode = mode;
    storageSaveWifiMode(wifiMode);
    if (wifiMode == WIFI_MODE_AUTO_NETWORKS) {
      // Reinicia la ventana de cierre automatico: si ya habian pasado
      // los 5 min mientras estaba en modo permanente, el AP no debe
      // cerrarse de golpe nada mas volver a modo automatico.
      apOpenedAt = millis();
    }
    Serial.printf("[WIFI] Modo de conexion cambiado a: %s\n",
      wifiMode == WIFI_MODE_AP_PERMANENT ? "AP PERMANENTE" : "REDES DISPONIBLES");
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/networks", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    request->send(200, "application/json", buildKnownNetworksJson());
  });

  server.on("/mover", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    int index = request->arg("index").toInt();
    int delta = request->arg("delta").toInt();
    storageMoveKnownNetwork(index, delta);
    request->send(200, "text/plain", "OK");
  });

  server.on("/eliminar", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    int index = request->arg("index").toInt();
    storageDeleteKnownNetwork(index);
    request->send(200, "text/plain", "OK");
  });

  server.on("/scan-wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    int n = WiFi.scanComplete();
    if (n < 0) { WiFi.scanNetworks(true); n = 0; }
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  server.on("/guardar-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    String ssid = request->arg("ssid");
    String pass = request->arg("password");
    Serial.printf("[WIFI] Red añadida desde el portal: %s\n", ssid.c_str());
    storageSaveKnownNetwork(ssid, pass);
    request->send(200, "text/plain", "OK");
  });

  server.on("/salir", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    Serial.println("[WIFI] Guardado confirmado desde el portal, reiniciando...");
    request->send(200, "text/plain", "OK");
    delay(800);
    ESP.restart();
  });

  // Acceso directo al portal de configuracion, util cuando la raiz "/"
  // esta sirviendo la app de control (modo AP permanente) o cuando se
  // navega desde la red domestica tras activar el AP bajo demanda.
  server.on("/wifi-config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!apActive) { request->send(404); return; }
    wifiSendConfigPage(request);
  });

  // Cualquier otra ruta no reconocida QUE LLEGUE POR EL AP: sirve la
  // pagina de configuracion (esto es lo que dispara la deteccion
  // automatica de captive portal en Android/iOS/Windows). Si la peticion
  // llega por la red domestica, se devuelve un 404 normal.
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (wifiRequestIsFromAp(request)) {
      wifiSendConfigPage(request);
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });

  routesRegistered = true;
}

// Arranca el punto de acceso + DNS del captive portal. Las rutas HTTP
// solo se registran la primera vez (routesRegistered evita duplicados
// si el AP se activa y desactiva varias veces durante la misma sesion).
static void startConfigPortal() {
  // IP fija y explicita del AP: en combinacion AP+STA, algunos ESP32 no
  // arrancan bien el DHCP del punto de acceso sin esta llamada previa.
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_CONFIG_SSID, AP_CONFIG_PASSWORD);

  apActive = true;
  apOpenedAt = millis();

  // Todas las consultas DNS del cliente conectado al AP se resuelven a
  // la IP del propio ESP32: es la mitad del truco del captive portal.
  dnsServer.start(53, "*", apIP);

  if (!routesRegistered) registerCaptiveRoutes();

  Serial.printf("[WIFI] Captive portal activo %lu min. Conectate a la red \"%s\" (pass: %s)\n",
    AP_CONFIG_WINDOW_MS / 60000UL, AP_CONFIG_SSID, AP_CONFIG_PASSWORD);
}

static void closeConfigPortal() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("[WIFI] Captive portal cerrado (ya conectado a la red domestica)");
}

// ---------------- Funciones publicas ----------------

void setupWifi() {
  WiFi.mode(WIFI_AP_STA); // AP y STA simultaneos: no son excluyentes en el ESP32
  WiFi.setHostname(DEVICE_HOSTNAME);

  storageLoadWifiMode(wifiMode);
  Serial.printf("[WIFI] Modo de conexion cargado: %s\n",
    wifiMode == WIFI_MODE_AP_PERMANENT ? "AP PERMANENTE" : "REDES DISPONIBLES");

  startConfigPortal(); // red de seguridad: se abre siempre al arrancar

  if (wifiMode == WIFI_MODE_AP_PERMANENT) {
    Serial.println("[WIFI] Modo AP permanente: no se intentara conectar a ninguna red domestica");
    return;
  }

  String ssid, pass;
  if (findBestKnownNetwork(ssid, pass) >= 0) {
    tryConnect(ssid, pass, 15000);
  }
  // Si no conecto aqui, loopWifi() seguira reintentando periodicamente
}

void loopWifi() {
  if (apActive) {
    dnsServer.processNextRequest();

    // Cierra el AP solo si: ya paso la ventana de tiempo Y ya hay STA
    // conectado Y no estamos en modo AP permanente. En modo permanente
    // el AP nunca se cierra solo, sea cual sea el estado de la STA.
    if (wifiMode != WIFI_MODE_AP_PERMANENT &&
        millis() - apOpenedAt > AP_CONFIG_WINDOW_MS && WiFi.status() == WL_CONNECTED) {
      closeConfigPortal();
    }
  }

  if (wifiMode == WIFI_MODE_AP_PERMANENT) return; // no se intenta STA en este modo

  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > 10000) {
    lastReconnectAttempt = millis();
    Serial.println("[WIFI] Sin conexion STA, reintentando...");
    String ssid, pass;
    if (findBestKnownNetwork(ssid, pass) >= 0) {
      WiFi.begin(ssid.c_str(), pass.c_str());
    }
  }
}

void wifiActivateConfigAp() {
  if (apActive) return; // ya esta activo, no hay nada que hacer
  Serial.println("[WIFI] Activando captive portal bajo demanda (solicitado desde la app)");
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  startConfigPortal();
}

bool wifiRequestIsFromAp(AsyncWebServerRequest *request) {
  return apActive && request->client()->localIP() == WiFi.softAPIP();
}

void wifiSendConfigPage(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", CONFIG_PAGE);
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool wifiIsApActive() {
  return apActive;
}

bool wifiIsPermanentApMode() {
  return wifiMode == WIFI_MODE_AP_PERMANENT;
}
