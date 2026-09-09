/*
 * tiras_led.cpp
 * -----------------------------------------------------------------------
 * Implementacion del modulo de tiras de LED BLE genericas.
 *
 * Protocolo BLE usado (tiras genericas tipo ELK-BLEDOM / MELK / LEDBLE,
 * muy extendidas en AliExpress y compatibles con la app "Lotus Lantern"):
 *   - Servicio        0000FFF0-0000-1000-8000-00805F9B34FB
 *   - Caracteristica   0000FFF3-0000-1000-8000-00805F9B34FB  (write, sin respuesta)
 *   - Todos los paquetes son de 9 bytes: 7E ... EF
 *   - Encender:        7E 04 04 01 00 00 00 00 EF
 *   - Apagar:          7E 04 04 00 00 00 00 00 EF
 *   - Color RGB:       7E 00 05 03 RR GG BB 00 EF
 * El brillo NO existe como comando independiente fiable en todas las
 * variantes de este protocolo: se simula escalando R/G/B antes de
 * enviarlos (enfoque estandar y el que menos memoria/librerias consume).
 * -----------------------------------------------------------------------
 */
#include "tiras_led.h"
#include "web_server.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// ---------------------------- Configuracion --------------------------------
// ============================================================================
#define LEDS_MAX_STRIPS        6     // Tiras maximas en el grupo
#define LEDS_MAX_PROGRAMS      5     // Programas horarios maximos
#define LEDS_SCAN_SECONDS      6     // Duracion de cada escaneo bajo demanda
#define LEDS_RECONNECT_MIN_MS  4000  // Backoff de reconexion: minimo
#define LEDS_RECONNECT_MAX_MS  30000 // Backoff de reconexion: maximo
#define LEDS_RECONNECT_STEP_MS 4000  // Backoff de reconexion: incremento
#define LEDS_CONNECT_TIMEOUT_MS 4000 // Timeout maximo para un intento de conexion BLE
// Limite de conexiones BLE SIMULTANEAS que este firmware intentara abrir.
// OJO: esto NO es solo un capricho de diseno, es una proteccion real: la
// libreria NimBLE-Arduino reserva en tiempo de COMPILACION un numero fijo
// de "slots" de conexion (CONFIG_BT_NIMBLE_MAX_CONNECTIONS, 3 por
// defecto). Intentar una conexion por encima de ese limite corrompe la
// pila BLE y reinicia el ESP32. Este valor DEBE ser <= al configurado en
// nimconfig.h de la libreria (ver comentario en ledsInit()). Se deja en 3
// porque es el valor por defecto de la libreria sin tocar nada; si subes
// ese valor en nimconfig.h, sube tambien esta constante a la vez.
#define LEDS_MAX_CONCURRENT_CONNECTIONS 3

// UUIDs del servicio/caracteristica BLE de las tiras ELK-BLEDOM/MELK/LEDBLE
static const char* LEDS_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
static const char* LEDS_CHAR_UUID    = "0000fff3-0000-1000-8000-00805f9b34fb";

// Los 15 efectos acordados. NONE = color estatico sin animacion.
enum LedEffect {
  EFFECT_NONE = 0,
  EFFECT_FLASH,
  EFFECT_FADE_2COLOR,
  EFFECT_BREATH,
  EFFECT_RAINBOW,
  EFFECT_BLINK,
  EFFECT_CHASE,
  EFFECT_FADE_MULTI,
  EFFECT_SPARKLE,
  EFFECT_COLOR_WIPE,
  EFFECT_TWINKLE,
  EFFECT_PULSE,
  EFFECT_ALTERNATE,
  EFFECT_FIRE,
  EFFECT_MUSIC_SIM,
  EFFECT_COUNT // centinela: numero total de efectos
};

// ============================================================================
// ------------------------------ Estructuras ---------------------------------
// ============================================================================

// Una tira emparejada dentro del grupo
struct LedStrip {
  bool     used = false;          // slot ocupado
  String   mac;                   // direccion MAC (identificador persistente)
  String   name;                  // nombre visible (del anuncio BLE, o la MAC si no hay)
  bool     connected = false;     // conectada ahora mismo
  unsigned long nextRetryMs = 0;  // millis() en el que se reintentara conectar
  unsigned long retryDelayMs = LEDS_RECONNECT_MIN_MS; // backoff actual
  NimBLEClient* client = nullptr;             // cliente NimBLE de esta tira
  NimBLERemoteCharacteristic* writeChar = nullptr; // caracteristica de escritura
};

// Un programa horario de encendido/apagado del grupo de LEDs
struct LedProgram {
  bool    enabled     = false;
  uint8_t startHour   = 20;
  uint8_t startMinute = 0;
  uint8_t endHour     = 23;
  uint8_t endMinute   = 0;
  bool    days[7]     = {false,false,false,false,false,false,false}; // 0=Domingo..6=Sabado
};

// ============================================================================
// ------------------------------ Estado global -------------------------------
// (privado a este .cpp: nada se expone fuera salvo ledsInit()/ledsLoop())
// ============================================================================
static LedStrip    g_strips[LEDS_MAX_STRIPS];
static LedProgram  g_programs[LEDS_MAX_PROGRAMS];

static uint8_t  g_colorR = 255, g_colorG = 120, g_colorB = 0; // color base actual
static uint8_t  g_colorR2 = 0,  g_colorG2 = 0,  g_colorB2 = 255; // segundo color (fades/alternancia)
static uint8_t  g_brightness = 100;   // 0-100 %
static bool     g_power = false;      // ON/OFF general del grupo
static LedEffect g_effect = EFFECT_NONE;
static uint8_t  g_speed = 50;         // 0-100 %, a mas valor mas rapido

static Preferences g_prefs;           // namespace propio en NVS, autocontenido

// --- Escaneo BLE bajo demanda ---
// El escaneo corre en su PROPIA tarea FreeRTOS (nucleo 0), separada del
// loop()/WebServer/AsyncTCP (nucleo 1). Se hace con llamadas BLOQUEANTES
// a NimBLEScan::getResults() en varias pasadas cortas: es el patron que
// de verdad encuentra los adverts, a diferencia de un scan->start(...)
// asincrono compitiendo con el resto del sistema en el mismo nucleo.
static bool      g_scanRunning = false;
struct ScanResult { String mac; String name; int rssi; };
#define LEDS_MAX_SCAN_RESULTS 15
static ScanResult g_scanResults[LEDS_MAX_SCAN_RESULTS];
static int        g_scanResultCount = 0;
// Protege g_scanRunning/g_scanResults/g_scanResultCount: se escriben desde
// la tarea de escaneo (nucleo 0) y se leen desde el handler HTTP
// "/api/leds/scanresults" (nucleo 1, dentro de loop()).
static SemaphoreHandle_t g_scanMutex = nullptr;

// Protege TODAS las llamadas a la API de NimBLE sobre clientes (connect,
// disconnect, getService, getCharacteristic, writeValue) y los campos
// client/writeChar/connected de LedStrip. Sin esto, la tarea de
// reconexion (nucleo 0), los comandos web (tarea AsyncTCP) y los
// callbacks de NimBLE (su propia tarea interna) pueden tocar el mismo
// cliente BLE a la vez, lo que provoca cuelgues y reinicios aleatorios.
static SemaphoreHandle_t g_bleMutex = nullptr;

// --- Presencia del usuario en la pagina "/leds" ---
// Todo lo relacionado con BLE (escaneo, reconexion, comandos manuales)
// solo debe ocurrir mientras alguien tiene la pagina "/leds" abierta, para
// no generar trafico BLE (ni riesgo de coexistencia con el WiFi) cuando
// nadie la esta viendo. La UNICA excepcion son los programas horarios
// (encendido/apagado automatico), que deben funcionar siempre.
static volatile unsigned long g_lastWebPresenceMs = 0;
// Ventana de tiempo tras un cambio de programa horario durante la que SI
// se permite conectar/enviar aunque no haya nadie en la pagina: el tiempo
// justo para que la tarea de reconexion conecte las tiras pendientes y
// les entregue el nuevo estado.
static volatile unsigned long g_scheduleWantsConnectionUntilMs = 0;

#define LEDS_PRESENCE_TIMEOUT_MS 3000            // ~2 ciclos del poll de la web (1200ms)
#define LEDS_SCHEDULE_CONNECT_WINDOW_MS 60000UL  // margen para conectar todas las tiras tras un cambio de programa

// Marca que hay alguien viendo/usando la pagina "/leds" ahora mismo.
// Llamar desde cualquier endpoint que solo tenga sentido con la pagina
// abierta (la propia pagina, su poll de estado, sus comandos, el escaneo).
static void ledsMarkWebPresence() {
  g_lastWebPresenceMs = millis();
}

// true si hay alguien con la pagina "/leds" abierta ahora mismo (se ha
// visto un poll suyo hace menos de LEDS_PRESENCE_TIMEOUT_MS).
static bool ledsUserPresent() {
  return (millis() - g_lastWebPresenceMs) < LEDS_PRESENCE_TIMEOUT_MS;
}

// true si el modulo tiene permiso para usar el radio BLE ahora mismo:
// o hay alguien en la pagina, o un programa horario acaba de cambiar de
// estado y todavia esta dentro de su ventana para conectar y aplicarlo.
static bool ledsBleAllowedNow() {
  return ledsUserPresent() || (long)(g_scheduleWantsConnectionUntilMs - millis()) > 0;
}

// Pequena ayuda RAII para no olvidar nunca soltar un mutex, incluso si hay
// un "return" en medio de la seccion critica. Reutilizable en cualquier
// otro modulo que necesite el mismo patron con un SemaphoreHandle_t.
class LedsMutexGuard {
public:
  explicit LedsMutexGuard(SemaphoreHandle_t m) : m_mutex(m) {
    if (m_mutex) xSemaphoreTake(m_mutex, portMAX_DELAY);
  }
  ~LedsMutexGuard() {
    if (m_mutex) xSemaphoreGive(m_mutex);
  }
private:
  SemaphoreHandle_t m_mutex;
};

// --- Estado interno del motor de efectos (no persistente) ---
static unsigned long g_effectLastStepMs = 0;
static uint16_t g_effectStep = 0; // contador de pasos generico, cada efecto lo interpreta a su manera

// --- Estado de aplicacion del programa horario (para no repetir logs/acciones) ---
static bool g_scheduleForcedState = false; // ultimo estado ON/OFF aplicado por el programa

// Declaraciones adelantadas (funciones privadas de este archivo)
static void ledsApplyColorToAllStrips(uint8_t r, uint8_t g, uint8_t b);
static void ledsApplyPowerToAllStrips(bool on);
static void ledsSaveGroup();
static void ledsLoadGroup();
static void ledsSaveSettings();
static void ledsLoadSettings();
static void ledsSavePrograms();
static void ledsLoadPrograms();
static void ledsTryConnectStrip(LedStrip &s);
static void ledsReconnectTaskFunc(void* pvParameters);
static void ledsStepEffect();
static void ledsRegisterWebRoutes();
static String ledsBuildStateJson();

// ============================================================================
// --------------------------- Utilidades reutilizables ------------------------
// Funciones sueltas, sin dependencias del resto del proyecto: se pueden
// copiar tal cual a otro proyecto que use tiras BLE genericas con este
// mismo protocolo.
// ============================================================================

// Escala un canal de color segun el brillo (0-100%). Reutilizable para
// cualquier efecto o conversion de color en proyectos con LEDs.
static uint8_t ledsScaleChannel(uint8_t value, uint8_t brightnessPct) {
  return (uint8_t)((uint16_t)value * brightnessPct / 100);
}

// Interpola linealmente entre dos valores de 0-255 segun una fase 0.0-1.0.
// Reutilizable en cualquier efecto de fade/transicion de color.
static uint8_t ledsLerp(uint8_t a, uint8_t b, float phase) {
  if (phase < 0) phase = 0;
  if (phase > 1) phase = 1;
  return (uint8_t)(a + (b - a) * phase);
}

// Convierte un valor de "velocidad" (0-100, a mas valor mas rapido) en un
// intervalo en milisegundos entre pasos de animacion. Reutilizable en
// cualquier motor de efectos LED con control de velocidad por slider.
static uint16_t ledsSpeedToIntervalMs(uint8_t speedPct, uint16_t minMs, uint16_t maxMs) {
  if (speedPct > 100) speedPct = 100;
  // A velocidad 100 -> minMs (rapido). A velocidad 0 -> maxMs (lento).
  return (uint16_t)(maxMs - ((uint32_t)(maxMs - minMs) * speedPct / 100));
}

// Genera un color HSV->RGB simple (H:0-359, S/V:0-255). Reutilizable para
// cualquier efecto tipo arcoiris/rainbow en proyectos con LEDs RGB.
static void ledsHsvToRgb(uint16_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  h = h % 360;
  float hf = h / 60.0f;
  float sf = s / 255.0f;
  float vf = v / 255.0f;
  int i = (int)hf;
  float f = hf - i;
  float p = vf * (1 - sf);
  float q = vf * (1 - sf * f);
  float t = vf * (1 - sf * (1 - f));
  float rf, gf, bf;
  switch (i % 6) {
    case 0: rf = vf; gf = t;  bf = p;  break;
    case 1: rf = q;  gf = vf; bf = p;  break;
    case 2: rf = p;  gf = vf; bf = t;  break;
    case 3: rf = p;  gf = q;  bf = vf; break;
    case 4: rf = t;  gf = p;  bf = vf; break;
    default:rf = vf; gf = p;  bf = q;  break;
  }
  r = (uint8_t)(rf * 255);
  g = (uint8_t)(gf * 255);
  b = (uint8_t)(bf * 255);
}

// ============================================================================
// -------------------------- Envio de comandos BLE ----------------------------
// ============================================================================

// Envia un comando crudo (array de bytes) a una tira ya conectada.
// Reutilizable para cualquier comando del protocolo ELK-BLEDOM/generico.
static void ledsSendRaw(LedStrip &s, const uint8_t *data, size_t len) {
  if (!s.connected || s.writeChar == nullptr) return;
  // "true" = write sin respuesta (write-without-response): mas rapido y
  // es lo que esperan estas tiras; evita bloquear esperando ACK.
  s.writeChar->writeValue((uint8_t*)data, len, false);
}

// Envia color RGB (ya con el brillo aplicado) a una tira concreta.
static void ledsSendColorToStrip(LedStrip &s, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[9] = {0x7E, 0x00, 0x05, 0x03, r, g, b, 0x00, 0xEF};
  ledsSendRaw(s, cmd, sizeof(cmd));
}

// Envia encendido/apagado a una tira concreta.
static void ledsSendPowerToStrip(LedStrip &s, bool on) {
  uint8_t cmd[9] = {0x7E, 0x04, 0x04, (uint8_t)(on ? 0x01 : 0x00), 0x00, 0x00, 0x00, 0x00, 0xEF};
  ledsSendRaw(s, cmd, sizeof(cmd));
}

// Aplica un color (con brillo ya escalado) a TODAS las tiras conectadas
// del grupo. Es la funcion que usan tanto los comandos manuales como
// cada paso de los efectos.
static void ledsApplyColorToAllStrips(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t sr = ledsScaleChannel(r, g_brightness);
  uint8_t sg = ledsScaleChannel(g, g_brightness);
  uint8_t sb = ledsScaleChannel(b, g_brightness);
  LedsMutexGuard guard(g_bleMutex);
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendColorToStrip(g_strips[i], sr, sg, sb);
    }
  }
}

// Aplica encendido/apagado a todas las tiras conectadas del grupo.
static void ledsApplyPowerToAllStrips(bool on) {
  LedsMutexGuard guard(g_bleMutex);
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendPowerToStrip(g_strips[i], on);
    }
  }
}

// ============================================================================
// ----------------------------- Conexion BLE ----------------------------------
// ============================================================================

// Callbacks de conexion/desconexion de un cliente NimBLE concreto. Se
// necesita una instancia por tira porque NimBLE identifica la tira que
// se desconecto a traves de este objeto.
class LedsClientCallbacks : public NimBLEClientCallbacks {
public:
  explicit LedsClientCallbacks(int stripIndex) : idx(stripIndex) {}
  void onDisconnect(NimBLEClient* client, int reason) override {
    LedsMutexGuard guard(g_bleMutex);
    if (idx < 0 || idx >= LEDS_MAX_STRIPS) return;
    g_strips[idx].connected = false;
    g_strips[idx].writeChar = nullptr;
    g_strips[idx].nextRetryMs = millis() + g_strips[idx].retryDelayMs;
    Serial.printf("[LEDS] Tira %s desconectada, reintento en %lums\n",
                  g_strips[idx].mac.c_str(), g_strips[idx].retryDelayMs);
  }
private:
  int idx;
};

// Intenta conectar (de forma NO bloqueante para el resto del sistema:
// NimBLE hace la conexion en su propia tarea interna, pero la llamada de
// connect() en si tarda hasta unos segundos, por eso solo se invoca cada
// "retryDelayMs" y nunca en cada vuelta del loop) una tira concreta del
// grupo, y localiza su caracteristica de escritura.
static void ledsTryConnectStrip(LedStrip &s) {
  LedsMutexGuard guard(g_bleMutex);
  if (s.connected) return;

  // Proteccion critica: nunca abrir mas conexiones BLE simultaneas de las
  // que la libreria NimBLE tiene reservadas (ver comentario en
  // LEDS_MAX_CONCURRENT_CONNECTIONS). Si ya estamos al limite, no se
  // intenta conectar esta tira ahora: se reintentara mas adelante (por si
  // otra tira se desconecta y libera un hueco), en vez de arriesgarse a
  // reiniciar el ESP32.
  int connectedNow = 0;
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) connectedNow++;
  }
  if (connectedNow >= LEDS_MAX_CONCURRENT_CONNECTIONS) {
    s.nextRetryMs = millis() + LEDS_RECONNECT_MIN_MS;
    return;
  }

  Serial.printf("[LEDS] Conectando con tira %s...\n", s.mac.c_str());

  if (s.client == nullptr) {
    s.client = NimBLEDevice::createClient();
    if (s.client == nullptr) {
      // No deberia pasar (el contador de arriba ya limita a
      // LEDS_MAX_CONCURRENT_CONNECTIONS), pero si ocurriera, NUNCA seguir
      // adelante con un puntero nulo: eso es justo lo que causaba el
      // panic (StoreProhibited) al añadir la 4a tira.
      Serial.printf("[LEDS] Sin slots BLE libres para %s, reintentando mas tarde\n", s.mac.c_str());
      s.nextRetryMs = millis() + LEDS_RECONNECT_MIN_MS;
      return;
    }
    int idx = (int)(&s - &g_strips[0]);
    s.client->setClientCallbacks(new LedsClientCallbacks(idx), true);
  }
  // Sin esto, un intento de conexion a una tira apagada/lejana puede
  // quedarse colgado con el timeout por defecto de NimBLE (mucho mas
  // largo), congelando la tarea de reconexion (y antes, cuando esto se
  // llamaba desde loop(), el sistema entero) durante ese tiempo.
  s.client->setConnectTimeout(LEDS_CONNECT_TIMEOUT_MS);

  NimBLEAddress addr(std::string(s.mac.c_str()), BLE_ADDR_PUBLIC);
  bool ok = s.client->connect(addr);
  if (!ok) {
    Serial.printf("[LEDS] Fallo al conectar con %s\n", s.mac.c_str());
    // CRITICO: liberar el slot de conexion ahora mismo. Sin este delete,
    // una tira que nunca responde se queda con su cliente BLE creado para
    // siempre, ocupando uno de los (pocos) slots de NimBLE de por vida:
    // exactamente lo que agotaba el pool al llegar a la 4a tira.
    NimBLEDevice::deleteClient(s.client);
    s.client = nullptr;
    s.retryDelayMs = min((unsigned long)LEDS_RECONNECT_MAX_MS, s.retryDelayMs + LEDS_RECONNECT_STEP_MS);
    s.nextRetryMs = millis() + s.retryDelayMs;
    return;
  }

  NimBLERemoteService* service = s.client->getService(LEDS_SERVICE_UUID);
  if (service == nullptr) {
    Serial.printf("[LEDS] %s conectada pero sin el servicio esperado\n", s.mac.c_str());
    s.client->disconnect();
    NimBLEDevice::deleteClient(s.client); // mismo motivo: liberar el slot
    s.client = nullptr;
    s.retryDelayMs = min((unsigned long)LEDS_RECONNECT_MAX_MS, s.retryDelayMs + LEDS_RECONNECT_STEP_MS);
    s.nextRetryMs = millis() + s.retryDelayMs;
    return;
  }
  s.writeChar = service->getCharacteristic(LEDS_CHAR_UUID);
  if (s.writeChar == nullptr) {
    Serial.printf("[LEDS] %s conectada pero sin la caracteristica esperada\n", s.mac.c_str());
    s.client->disconnect();
    NimBLEDevice::deleteClient(s.client); // mismo motivo: liberar el slot
    s.client = nullptr;
    s.retryDelayMs = min((unsigned long)LEDS_RECONNECT_MAX_MS, s.retryDelayMs + LEDS_RECONNECT_STEP_MS);
    s.nextRetryMs = millis() + s.retryDelayMs;
    return;
  }

  s.connected = true;
  s.retryDelayMs = LEDS_RECONNECT_MIN_MS; // exito: resetea el backoff
  Serial.printf("[LEDS] Tira %s conectada correctamente\n", s.mac.c_str());

  // Al reconectar, se re-envia el estado actual para que la tira quede
  // igual que el resto del grupo (color/brillo/on-off vigentes).
  ledsSendPowerToStrip(s, g_power);
  if (g_power) {
    ledsSendColorToStrip(s, ledsScaleChannel(g_colorR, g_brightness),
                             ledsScaleChannel(g_colorG, g_brightness),
                             ledsScaleChannel(g_colorB, g_brightness));
  }
}

// Añade (o refresca RSSI/nombre de) un resultado de escaneo a la lista
// compartida. El llamante debe tener ya g_scanMutex tomado.
static void ledsAddScanResultLocked(const String &mac, const String &name, int rssi) {
  for (int i = 0; i < g_scanResultCount; i++) {
    if (g_scanResults[i].mac == mac) {
      g_scanResults[i].rssi = rssi;
      if (name.length() > 0) g_scanResults[i].name = name;
      return;
    }
  }
  if (g_scanResultCount >= LEDS_MAX_SCAN_RESULTS) return;
  g_scanResults[g_scanResultCount].mac  = mac;
  g_scanResults[g_scanResultCount].name = name.length() > 0 ? name : mac;
  g_scanResults[g_scanResultCount].rssi = rssi;
  g_scanResultCount++;
}

// Cuerpo de la tarea FreeRTOS dedicada al escaneo BLE. Se ejecuta en el
// nucleo 0 (loop()/WebServer/AsyncTCP siguen en el nucleo 1) y usa
// llamadas BLOQUEANTES a NimBLEScan::getResults() en varias pasadas
// cortas: mas fiable para capturar adverts que un scan asincrono
// compitiendo con el resto del sistema en el mismo nucleo. Se borra a si
// misma (vTaskDelete(NULL)) al terminar.
static void ledsScanTaskFunc(void* pvParameters) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  // Duty cycle moderado (~50%): un valor demasiado agresivo perjudica la
  // coexistencia WiFi/BLE (comparten el mismo radio en el ESP32) y hace
  // perder MAS paquetes de advertising, no menos.
  scan->setInterval(100); // 100 * 0.625ms = 62.5ms
  scan->setWindow(50);    // 50  * 0.625ms = 31.25ms (~50% duty cycle)

  const int kPasses = 3;
  uint32_t passMs = (LEDS_SCAN_SECONDS * 1000UL) / kPasses;
  if (passMs < 800) passMs = 800;

  for (int pass = 0; pass < kPasses; pass++) {
    NimBLEScanResults results = scan->getResults(passMs, false);

    LedsMutexGuard guard(g_scanMutex);
    for (int i = 0; i < results.getCount(); i++) {
      const NimBLEAdvertisedDevice* dev = results.getDevice(i);
      if (!dev) continue;
      String mac  = dev->getAddress().toString().c_str();
      String name = dev->haveName() ? dev->getName().c_str() : "";
      ledsAddScanResultLocked(mac, name, dev->getRSSI());
    }
    scan->clearResults();

    if (pass < kPasses - 1) vTaskDelay(pdMS_TO_TICKS(80)); // cede CPU real entre pasadas
  }

  {
    LedsMutexGuard guard(g_scanMutex);
    g_scanRunning = false;
    Serial.printf("[LEDS] Escaneo BLE finalizado, %d dispositivo(s) encontrado(s)\n", g_scanResultCount);
  }

  vTaskDelete(nullptr);
}

// Inicia un escaneo BLE de duracion limitada (LEDS_SCAN_SECONDS), en su
// propia tarea. Se invoca solo cuando el usuario pulsa "Buscar tiras" en
// la web, nunca de forma permanente, para minimizar el impacto sobre el
// WiFi. Si ya hay un escaneo en curso, no se lanza uno nuevo.
static void ledsStartScan() {
  if (!ledsUserPresent()) return; // el escaneo solo tiene sentido con la pagina abierta
  {
    LedsMutexGuard guard(g_scanMutex);
    if (g_scanRunning) return;
    g_scanResultCount = 0;
    g_scanRunning = true;
  }
  // Stack 8192 (no 4096): NimBLEScan::getResults() y el parseo de adverts
  // pueden necesitar mas pila de la que parece a simple vista; con 4096
  // se arriesga corrupcion silenciosa de pila sin llegar a un panic
  // completo del sistema.
  xTaskCreatePinnedToCore(ledsScanTaskFunc, "ledsScan", 8192, nullptr, 1, nullptr, 0);
  Serial.println("[LEDS] Escaneo BLE iniciado");
}

// ============================================================================
// -------------------------------- Efectos -------------------------------------
// Motor de efectos no bloqueante: se llama en cada vuelta de ledsLoop(),
// pero solo actua cuando ha pasado el intervalo correspondiente a la
// velocidad configurada (g_speed). Ningun efecto usa delay().
// ============================================================================
static void ledsStepEffect() {
  if (!g_power || g_effect == EFFECT_NONE) return;

  // Cada efecto define su propio rango de velocidad (algunos necesitan
  // pasos mas rapidos que otros para verse bien).
  uint16_t interval;
  switch (g_effect) {
    case EFFECT_FLASH:      interval = ledsSpeedToIntervalMs(g_speed, 40, 800);  break;
    case EFFECT_BLINK:      interval = ledsSpeedToIntervalMs(g_speed, 150, 1500); break;
    case EFFECT_SPARKLE:    interval = ledsSpeedToIntervalMs(g_speed, 30, 400);  break;
    case EFFECT_TWINKLE:    interval = ledsSpeedToIntervalMs(g_speed, 80, 600);  break;
    case EFFECT_MUSIC_SIM:  interval = ledsSpeedToIntervalMs(g_speed, 60, 500);  break;
    default:                interval = ledsSpeedToIntervalMs(g_speed, 20, 200);  break;
  }

  unsigned long now = millis();
  if (now - g_effectLastStepMs < interval) return;
  g_effectLastStepMs = now;
  g_effectStep++;

  uint8_t r = g_colorR, g = g_colorG, b = g_colorB;

  switch (g_effect) {

    case EFFECT_FLASH: {
      // Destello breve: alterna color a full brillo y apagado
      bool on = (g_effectStep % 2) == 0;
      if (on) ledsApplyColorToAllStrips(255, 255, 255);
      else    ledsApplyColorToAllStrips(0, 0, 0);
      return; // ya aplicado con brillo propio del efecto, no re-escalar abajo
    }

    case EFFECT_FADE_2COLOR: {
      // Transicion suave y ciclica entre color 1 y color 2
      float phase = (sinf(g_effectStep * 0.05f) + 1.0f) / 2.0f;
      r = ledsLerp(g_colorR, g_colorR2, phase);
      g = ledsLerp(g_colorG, g_colorG2, phase);
      b = ledsLerp(g_colorB, g_colorB2, phase);
      break;
    }

    case EFFECT_BREATH: {
      // "Respiracion": el color base sube y baja de brillo suavemente
      float phase = (sinf(g_effectStep * 0.08f) + 1.0f) / 2.0f; // 0..1
      uint8_t localBrightness = (uint8_t)(20 + phase * 80); // nunca a 0 total, se ve mejor
      ledsApplyColorToAllStrips(ledsScaleChannel(r, localBrightness),
                                 ledsScaleChannel(g, localBrightness),
                                 ledsScaleChannel(b, localBrightness));
      return;
    }

    case EFFECT_RAINBOW: {
      // Ciclo continuo por todo el espectro de color
      uint16_t hue = (g_effectStep * 4) % 360;
      ledsHsvToRgb(hue, 255, 255, r, g, b);
      break;
    }

    case EFFECT_BLINK: {
      // Parpadeo simple on/off del color base (mas marcado que el flash)
      bool on = (g_effectStep % 2) == 0;
      ledsApplyColorToAllStrips(on ? r : 0, on ? g : 0, on ? b : 0);
      return;
    }

    case EFFECT_CHASE: {
      // Persecucion simulada mediante variacion ciclica de intensidad
      // (en tiras de un solo canal de color por segmento no direccionable
      // individualmente, se simula con un barrido de brillo)
      float phase = (float)(g_effectStep % 20) / 20.0f;
      uint8_t localBrightness = (uint8_t)(255 * (1.0f - fabsf(phase - 0.5f) * 2.0f));
      ledsApplyColorToAllStrips(ledsScaleChannel(r, (localBrightness * 100) / 255),
                                 ledsScaleChannel(g, (localBrightness * 100) / 255),
                                 ledsScaleChannel(b, (localBrightness * 100) / 255));
      return;
    }

    case EFFECT_FADE_MULTI: {
      // Secuencia de varios colores predefinidos, con fade entre ellos
      static const uint8_t palette[6][3] = {
        {255,0,0},{255,120,0},{255,255,0},{0,255,0},{0,120,255},{170,0,255}
      };
      int total = 6 * 40; // 40 pasos de fade por color
      int pos = g_effectStep % total;
      int idxA = (pos / 40) % 6;
      int idxB = (idxA + 1) % 6;
      float phase = (pos % 40) / 40.0f;
      r = ledsLerp(palette[idxA][0], palette[idxB][0], phase);
      g = ledsLerp(palette[idxA][1], palette[idxB][1], phase);
      b = ledsLerp(palette[idxA][2], palette[idxB][2], phase);
      break;
    }

    case EFFECT_SPARKLE: {
      // Destellos aleatorios de color blanco sobre el color base
      if (random(0, 4) == 0) {
        ledsApplyColorToAllStrips(255, 255, 255);
      } else {
        ledsApplyColorToAllStrips(r, g, b);
      }
      return;
    }

    case EFFECT_COLOR_WIPE: {
      // Barrido: transicion de "apagado" a "color" y vuelta, en bucle
      int pos = g_effectStep % 40;
      float phase = pos < 20 ? pos / 20.0f : (40 - pos) / 20.0f;
      ledsApplyColorToAllStrips(ledsScaleChannel(r, 100) * phase,
                                 ledsScaleChannel(g, 100) * phase,
                                 ledsScaleChannel(b, 100) * phase);
      return;
    }

    case EFFECT_TWINKLE: {
      // Parpadeos aleatorios suaves tipo "estrellas": brillo aleatorio
      // suavizado sobre el color base
      uint8_t localBrightness = 40 + random(0, 60);
      ledsApplyColorToAllStrips(ledsScaleChannel(r, localBrightness),
                                 ledsScaleChannel(g, localBrightness),
                                 ledsScaleChannel(b, localBrightness));
      return;
    }

    case EFFECT_PULSE: {
      // Respiracion mas rapida y marcada (mismo calculo que BREATH pero
      // con una frecuencia mayor y rango de brillo mas amplio)
      float phase = (sinf(g_effectStep * 0.25f) + 1.0f) / 2.0f;
      uint8_t localBrightness = (uint8_t)(phase * 100);
      ledsApplyColorToAllStrips(ledsScaleChannel(r, localBrightness),
                                 ledsScaleChannel(g, localBrightness),
                                 ledsScaleChannel(b, localBrightness));
      return;
    }

    case EFFECT_ALTERNATE: {
      // Alternancia neta entre color 1 y color 2
      bool first = (g_effectStep % 2) == 0;
      if (first) { r = g_colorR; g = g_colorG; b = g_colorB; }
      else       { r = g_colorR2; g = g_colorG2; b = g_colorB2; }
      break;
    }

    case EFFECT_FIRE: {
      // Efecto llama: tonos calidos con parpadeo aleatorio de intensidad
      uint8_t flicker = random(140, 255);
      r = flicker;
      g = (uint8_t)(flicker * 0.35f);
      b = 0;
      break;
    }

    case EFFECT_MUSIC_SIM: {
      // Simulacion de "beat": variacion pseudoaleatoria de brillo sobre
      // el color base, como si reaccionara a musica (sin microfono real)
      uint8_t localBrightness = random(20, 100);
      ledsApplyColorToAllStrips(ledsScaleChannel(r, localBrightness),
                                 ledsScaleChannel(g, localBrightness),
                                 ledsScaleChannel(b, localBrightness));
      return;
    }

    default:
      return;
  }

  ledsApplyColorToAllStrips(r, g, b);
}

// ============================================================================
// ------------------------------ Programas horarios ---------------------------
// ============================================================================

// Revisa los programas configurados y enciende/apaga el grupo cuando
// corresponde. Usa el reloj del sistema (NTP), igual que schedule.cpp.
static void ledsApplySchedule() {
  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  if (tmNow.tm_year < 100) return; // reloj aun no sincronizado por NTP, no actuar

  int nowMinutes = tmNow.tm_hour * 60 + tmNow.tm_min;
  int wday = tmNow.tm_wday; // 0=Domingo..6=Sabado, igual que ScheduleProgram

  bool shouldBeOn = false;
  for (int i = 0; i < LEDS_MAX_PROGRAMS; i++) {
    LedProgram &p = g_programs[i];
    if (!p.enabled || !p.days[wday]) continue;
    int startMin = p.startHour * 60 + p.startMinute;
    int endMin   = p.endHour * 60 + p.endMinute;
    if (startMin == endMin) continue; // programa vacio, ignorar
    if (startMin < endMin) {
      // Tramo normal dentro del mismo dia
      if (nowMinutes >= startMin && nowMinutes < endMin) shouldBeOn = true;
    } else {
      // Tramo que cruza medianoche (ej: 22:00 -> 02:00)
      if (nowMinutes >= startMin || nowMinutes < endMin) shouldBeOn = true;
    }
  }

  // Solo actua si cambia respecto al ultimo estado forzado por el
  // programa, para no pisar constantemente un ajuste manual del usuario
  // ni generar trafico BLE innecesario.
  if (shouldBeOn != g_scheduleForcedState) {
    g_scheduleForcedState = shouldBeOn;
    g_power = shouldBeOn;
    // Abre la ventana que permite a la tarea de reconexion conectar (sin
    // presencia en la web) las tiras necesarias para aplicar este cambio.
    g_scheduleWantsConnectionUntilMs = millis() + LEDS_SCHEDULE_CONNECT_WINDOW_MS;
    ledsApplyPowerToAllStrips(g_power);
    if (g_power) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    Serial.printf("[LEDS] Programa horario: grupo %s\n", g_power ? "ENCENDIDO" : "APAGADO");
  }
}

// ============================================================================
// --------------------------------- Persistencia -------------------------------
// Namespace propio en NVS ("leds"), totalmente autocontenido: no depende
// de storage.cpp ni comparte claves con el resto del proyecto.
// ============================================================================

static void ledsLoadGroup() {
  g_prefs.begin("leds", true); // solo lectura
  int count = g_prefs.getInt("stripCount", 0);
  for (int i = 0; i < count && i < LEDS_MAX_STRIPS; i++) {
    String macKey  = "mac"  + String(i);
    String nameKey = "name" + String(i);
    String mac = g_prefs.getString(macKey.c_str(), "");
    if (mac.length() == 0) continue;
    g_strips[i].used = true;
    g_strips[i].mac  = mac;
    g_strips[i].name = g_prefs.getString(nameKey.c_str(), mac);
  }
  g_prefs.end();
}

static void ledsSaveGroup() {
  g_prefs.begin("leds", false); // lectura/escritura
  int count = 0;
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (!g_strips[i].used) continue;
    String macKey  = "mac"  + String(count);
    String nameKey = "name" + String(count);
    g_prefs.putString(macKey.c_str(), g_strips[i].mac);
    g_prefs.putString(nameKey.c_str(), g_strips[i].name);
    count++;
  }
  g_prefs.putInt("stripCount", count);
  g_prefs.end();
}

static void ledsLoadSettings() {
  g_prefs.begin("leds", true);
  g_colorR      = g_prefs.getUChar("r", g_colorR);
  g_colorG      = g_prefs.getUChar("g", g_colorG);
  g_colorB      = g_prefs.getUChar("b", g_colorB);
  g_colorR2     = g_prefs.getUChar("r2", g_colorR2);
  g_colorG2     = g_prefs.getUChar("g2", g_colorG2);
  g_colorB2     = g_prefs.getUChar("b2", g_colorB2);
  g_brightness  = g_prefs.getUChar("bright", g_brightness);
  g_power       = g_prefs.getBool("power", g_power);
  g_effect      = (LedEffect)g_prefs.getUChar("effect", (uint8_t)g_effect);
  g_speed       = g_prefs.getUChar("speed", g_speed);
  g_prefs.end();
}

static void ledsSaveSettings() {
  g_prefs.begin("leds", false);
  g_prefs.putUChar("r", g_colorR);
  g_prefs.putUChar("g", g_colorG);
  g_prefs.putUChar("b", g_colorB);
  g_prefs.putUChar("r2", g_colorR2);
  g_prefs.putUChar("g2", g_colorG2);
  g_prefs.putUChar("b2", g_colorB2);
  g_prefs.putUChar("bright", g_brightness);
  g_prefs.putBool("power", g_power);
  g_prefs.putUChar("effect", (uint8_t)g_effect);
  g_prefs.putUChar("speed", g_speed);
  g_prefs.end();
}

static void ledsLoadPrograms() {
  g_prefs.begin("leds", true);
  for (int i = 0; i < LEDS_MAX_PROGRAMS; i++) {
    String pfx = "prog" + String(i) + "_";
    g_programs[i].enabled     = g_prefs.getBool((pfx + "en").c_str(), false);
    g_programs[i].startHour   = g_prefs.getUChar((pfx + "sh").c_str(), g_programs[i].startHour);
    g_programs[i].startMinute = g_prefs.getUChar((pfx + "sm").c_str(), g_programs[i].startMinute);
    g_programs[i].endHour     = g_prefs.getUChar((pfx + "eh").c_str(), g_programs[i].endHour);
    g_programs[i].endMinute   = g_prefs.getUChar((pfx + "em").c_str(), g_programs[i].endMinute);
    uint8_t daysMask = g_prefs.getUChar((pfx + "days").c_str(), 0);
    for (int d = 0; d < 7; d++) g_programs[i].days[d] = (daysMask >> d) & 0x01;
  }
  g_prefs.end();
}

static void ledsSavePrograms() {
  g_prefs.begin("leds", false);
  for (int i = 0; i < LEDS_MAX_PROGRAMS; i++) {
    String pfx = "prog" + String(i) + "_";
    g_prefs.putBool((pfx + "en").c_str(), g_programs[i].enabled);
    g_prefs.putUChar((pfx + "sh").c_str(), g_programs[i].startHour);
    g_prefs.putUChar((pfx + "sm").c_str(), g_programs[i].startMinute);
    g_prefs.putUChar((pfx + "eh").c_str(), g_programs[i].endHour);
    g_prefs.putUChar((pfx + "em").c_str(), g_programs[i].endMinute);
    uint8_t daysMask = 0;
    for (int d = 0; d < 7; d++) if (g_programs[i].days[d]) daysMask |= (1 << d);
    g_prefs.putUChar((pfx + "days").c_str(), daysMask);
  }
  g_prefs.end();
}

// ============================================================================
// ---------------------------------- Pagina web --------------------------------
// Mismo estilo visual (paleta, ancho 560px, fuente mono) que la app
// principal (ver webpage.cpp), para mantener coherencia entre paginas.
// ============================================================================
static const char LEDS_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LEDs - Jacuzzi ESP32</title>
<style>
:root{
  --bg:#0b1210; --panel:#101a17; --line:#22332c; --steel:#3a4a44;
  --amber:#e8a33d; --green:#4fd67a; --red:#e2513f; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;}
h2{font-size:11px;letter-spacing:1.5px;color:var(--dim);text-transform:uppercase;margin:14px 0 6px 4px;}
.wrap{max-width:560px;margin:0 auto;}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px;margin-top:8px;}
.row{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px;}
button{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;padding:6px 8px;font-family:var(--mono);font-size:11px;cursor:pointer;}
button.active{border-color:var(--amber);color:var(--amber);}
button.back{margin-bottom:8px;}
input[type=range]{width:100%;}
input[type=color]{width:44px;height:30px;border:1px solid var(--line);border-radius:4px;background:none;padding:0;}
input[type=time]{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;font-family:var(--mono);padding:3px 6px;}
.swatch{width:28px;height:28px;border-radius:4px;border:1px solid var(--line);cursor:pointer;}
.stripitem{display:flex;justify-content:space-between;align-items:center;padding:6px;border-bottom:1px solid var(--line);font-size:12px;}
.dim{color:var(--dim);font-size:11px;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:6px;background:#555;}
.dot.offline{background:var(--red);}
.daybtn{width:30px;height:26px;font-size:10px;}
</style>
</head>
<body>
<div class="wrap">
<button class="back" onclick="location.href='/'">&larr; VOLVER</button>
<h1>&#9679; ESP32 &middot; CONTROL TIRAS LED</h1>

<div class="panel">
  <h2>Encendido y color</h2>
  <div class="row">
    <button id="btnPower">ENCENDER / APAGAR</button>
    <input type="color" id="colorPicker" value="#ff7800">
  </div>
  <div class="dim" style="margin-top:8px;">Colores predeterminados</div>
  <div class="row" id="presets"></div>
  <div class="dim" style="margin-top:8px;">Brillo</div>
  <input type="range" id="brightness" min="0" max="100" value="100">
</div>

<div class="panel">
  <h2>Efectos</h2>
  <div class="row" id="effects"></div>
  <div class="dim" style="margin-top:8px;">Velocidad</div>
  <input type="range" id="speed" min="0" max="100" value="50">
</div>

<div class="panel">
  <h2>Tiras emparejadas</h2>
  <div id="stripList"></div>
  <div class="row">
    <button id="btnScan">BUSCAR TIRAS</button>
  </div>
  <div id="scanResults"></div>
</div>

<div class="panel">
  <h2>Programas</h2>
  <div id="programList"></div>
</div>

</div>
<script>
const el = id => document.getElementById(id);
const EFFECTS = ["Ninguno","Flash","Fade 2 colores","Respiracion","Arcoiris","Blink",
  "Persecucion","Fade multicolor","Sparkle","Color wipe","Twinkle","Pulso",
  "Alternancia","Fuego","Musica"];
const PRESETS = ["#ff0000","#ff7800","#ffff00","#00ff00","#00ffff","#0000ff","#ff00ff","#ffffff"];
const DAY_LABELS = ["D","L","M","X","J","V","S"];
let state = {};

function post(cmd, extra) {
  const body = Object.assign({cmd:cmd}, extra || {});
  fetch('/api/leds/command', {method:'POST', body: JSON.stringify(body)})
    .then(r => r.json()).then(refreshState);
}

function refreshState() {
  fetch('/api/leds/state').then(r => r.json()).then(s => {
    state = s;
    el('colorPicker').value = rgbToHex(s.r, s.g, s.b);
    el('brightness').value = s.brightness;
    el('speed').value = s.speed;
    el('btnPower').className = s.power ? 'active' : '';
    renderEffects();
    renderStrips();
    renderPrograms();
  });
}

function rgbToHex(r,g,b){ return '#' + [r,g,b].map(x=>x.toString(16).padStart(2,'0')).join(''); }
function hexToRgb(hex){ const n=parseInt(hex.slice(1),16); return {r:(n>>16)&255, g:(n>>8)&255, b:n&255}; }

function renderPresets() {
  el('presets').innerHTML = PRESETS.map(c =>
    `<div class="swatch" style="background:${c}" onclick="applyColor('${c}')"></div>`).join('');
}
function applyColor(hex) {
  const c = hexToRgb(hex);
  post('setColor', {r:c.r, g:c.g, b:c.b});
}

function renderEffects() {
  el('effects').innerHTML = EFFECTS.map((name,i) =>
    `<button class="${state.effect===i?'active':''}" onclick="post('setEffect',{effect:${i}})">${name}</button>`).join('');
}

function renderStrips() {
  if (!state.strips) return;
  el('stripList').innerHTML = state.strips.map((s,i) => {
    // El punto refleja el estado real: rojo si la tira no responde por
    // BLE, apagado (gris) si el grupo esta OFF, o el color/efecto en
    // curso si esta encendida. Se actualiza solo, junto al resto del
    // estado (ver setInterval de refreshState).
    let dotStyle = '';
    let dotClass = 'dot';
    if (!s.connected) { dotClass += ' offline'; }
    else if (state.power) { dotStyle = `background:rgb(${state.r},${state.g},${state.b})`; }
    return `<div class="stripitem"><span><span class="${dotClass}" style="${dotStyle}"></span>${s.name} <span class="dim">(${s.mac})</span></span>
     <button onclick="post('removeStrip',{index:${i}})">QUITAR</button></div>`;
  }).join('') || '<div class="dim">Sin tiras emparejadas todavia</div>';
}

function renderPrograms() {
  if (!state.programs) return;
  el('programList').innerHTML = state.programs.map((p,i) => `
    <div class="panel" style="margin-top:6px;">
      <div class="row">
        <button class="${p.enabled?'active':''}" onclick="toggleProgram(${i})">PROG ${i+1}: ${p.enabled?'ON':'OFF'}</button>
        <input type="time" value="${pad(p.startHour)}:${pad(p.startMinute)}" onchange="setProgTime(${i},'start',this.value)">
        <input type="time" value="${pad(p.endHour)}:${pad(p.endMinute)}" onchange="setProgTime(${i},'end',this.value)">
      </div>
      <div class="row">
        ${DAY_LABELS.map((d,dIdx) =>
          `<button class="daybtn ${p.days[dIdx]?'active':''}" onclick="toggleProgDay(${i},${dIdx})">${d}</button>`).join('')}
      </div>
    </div>`).join('');
}
function pad(n){ return String(n).padStart(2,'0'); }
function toggleProgram(i){ post('setProgram', {index:i, enabled: !state.programs[i].enabled}); }
function toggleProgDay(i,d){
  const days = state.programs[i].days.slice();
  days[d] = !days[d];
  post('setProgram', {index:i, days:days});
}
function setProgTime(i, which, value) {
  const [h,m] = value.split(':').map(Number);
  const patch = {index:i};
  if (which === 'start') { patch.startHour=h; patch.startMinute=m; }
  else { patch.endHour=h; patch.endMinute=m; }
  post('setProgram', patch);
}

el('btnPower').onclick = () => post('setPower', {power: !state.power});
el('colorPicker').onchange = () => applyColor(el('colorPicker').value);
el('brightness').onchange = () => post('setBrightness', {value: parseInt(el('brightness').value)});
el('speed').onchange = () => post('setSpeed', {value: parseInt(el('speed').value)});

el('btnScan').onclick = () => {
  el('scanResults').innerHTML = '<div class="dim">Buscando durante unos segundos...</div>';
  fetch('/api/leds/scan', {method:'POST'}).then(() => {
    setTimeout(pollScan, 2000);
  });
};
function pollScan() {
  fetch('/api/leds/scanresults').then(r => r.json()).then(data => {
    if (data.running) { setTimeout(pollScan, 1500); return; }
    el('scanResults').innerHTML = (data.results || []).map(r =>
      `<div class="stripitem"><span>${r.name} <span class="dim">(${r.mac}, ${r.rssi}dBm)</span></span>
       <button onclick="addStrip('${r.mac}','${r.name}')">AÑADIR</button></div>`).join('') || '<div class="dim">Nada encontrado</div>';
  });
}
function addStrip(mac, name) { post('addStrip', {mac:mac, name:name}); }

renderPresets();
refreshState();
setInterval(refreshState, 1200);
</script>
</body>
</html>
)HTMLPAGE";

// Construye el JSON de estado completo del modulo (para /api/leds/state).
static String ledsBuildStateJson() {
  StaticJsonDocument<1536> doc;
  doc["power"]      = g_power;
  doc["r"] = g_colorR; doc["g"] = g_colorG; doc["b"] = g_colorB;
  doc["r2"] = g_colorR2; doc["g2"] = g_colorG2; doc["b2"] = g_colorB2;
  doc["brightness"] = g_brightness;
  doc["effect"]     = (uint8_t)g_effect;
  doc["speed"]      = g_speed;

  JsonArray strips = doc.createNestedArray("strips");
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (!g_strips[i].used) continue;
    JsonObject o = strips.createNestedObject();
    o["mac"] = g_strips[i].mac;
    o["name"] = g_strips[i].name;
    o["connected"] = g_strips[i].connected;
  }

  JsonArray programs = doc.createNestedArray("programs");
  for (int i = 0; i < LEDS_MAX_PROGRAMS; i++) {
    JsonObject o = programs.createNestedObject();
    o["enabled"] = g_programs[i].enabled;
    o["startHour"] = g_programs[i].startHour;
    o["startMinute"] = g_programs[i].startMinute;
    o["endHour"] = g_programs[i].endHour;
    o["endMinute"] = g_programs[i].endMinute;
    JsonArray days = o.createNestedArray("days");
    for (int d = 0; d < 7; d++) days.add(g_programs[i].days[d]);
  }

  String out;
  serializeJson(doc, out);
  return out;
}

// Procesa un comando JSON recibido desde la pagina "/leds".
static void ledsHandleCommand(const String &jsonStr) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
    Serial.println("[LEDS] Comando con JSON invalido, se ignora");
    return;
  }
  String cmd = doc["cmd"] | "";

  if (cmd == "setPower") {
    g_power = doc["power"] | g_power;
    g_scheduleForcedState = g_power; // un cambio manual "adopta" el estado actual
    ledsApplyPowerToAllStrips(g_power);
    if (g_power) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    ledsSaveSettings();

  } else if (cmd == "setColor") {
    g_colorR = doc["r"] | g_colorR;
    g_colorG = doc["g"] | g_colorG;
    g_colorB = doc["b"] | g_colorB;
    g_effect = EFFECT_NONE; // elegir un color manual desactiva el efecto en curso
    if (g_power) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    ledsSaveSettings();

  } else if (cmd == "setBrightness") {
    g_brightness = constrain((int)(doc["value"] | g_brightness), 0, 100);
    if (g_power && g_effect == EFFECT_NONE) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    ledsSaveSettings();

  } else if (cmd == "setEffect") {
    uint8_t e = doc["effect"] | 0;
    if (e < EFFECT_COUNT) g_effect = (LedEffect)e;
    g_effectStep = 0;
    ledsSaveSettings();

  } else if (cmd == "setSpeed") {
    g_speed = constrain((int)(doc["value"] | g_speed), 0, 100);
    ledsSaveSettings();

  } else if (cmd == "addStrip") {
    String mac = doc["mac"] | "";
    String name = doc["name"] | mac;
    if (mac.length() > 0) {
      for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
        if (!g_strips[i].used) {
          g_strips[i] = LedStrip(); // reinicia el slot
          g_strips[i].used = true;
          g_strips[i].mac = mac;
          g_strips[i].name = name;
          g_strips[i].nextRetryMs = millis(); // intenta conectar ya
          ledsSaveGroup();
          Serial.printf("[LEDS] Tira anadida al grupo: %s (%s)\n", name.c_str(), mac.c_str());
          break;
        }
      }
    }

  } else if (cmd == "removeStrip") {
    int index = doc["index"] | -1;
    int seen = -1;
    for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
      if (!g_strips[i].used) continue;
      seen++;
      if (seen == index) {
        if (g_strips[i].client != nullptr && g_strips[i].connected) {
          LedsMutexGuard guard(g_bleMutex);
          g_strips[i].client->disconnect();
        }
        g_strips[i] = LedStrip();
        ledsSaveGroup();
        break;
      }
    }

  } else if (cmd == "setProgram") {
    int index = doc["index"] | -1;
    if (index >= 0 && index < LEDS_MAX_PROGRAMS) {
      LedProgram &p = g_programs[index];
      if (doc.containsKey("enabled"))     p.enabled     = doc["enabled"];
      if (doc.containsKey("startHour"))   p.startHour   = doc["startHour"];
      if (doc.containsKey("startMinute")) p.startMinute = doc["startMinute"];
      if (doc.containsKey("endHour"))     p.endHour     = doc["endHour"];
      if (doc.containsKey("endMinute"))   p.endMinute   = doc["endMinute"];
      if (doc.containsKey("days")) {
        JsonArray days = doc["days"];
        for (int d = 0; d < 7 && d < (int)days.size(); d++) p.days[d] = days[d];
      }
      ledsSavePrograms();
    }
  }
}

// Registra en el servidor web unico del proyecto la pagina "/leds" y su
// API JSON. Usa la instancia compartida (webServerInstance()), tal y
// como exige web_server.h, para no crear un segundo servidor.
static void ledsRegisterWebRoutes() {
  AsyncWebServer &server = webServerInstance();

  server.on("/leds", HTTP_GET, [](AsyncWebServerRequest *request) {
    ledsMarkWebPresence();
    request->send_P(200, "text/html", LEDS_HTML);
  });

  server.on("/api/leds/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    ledsMarkWebPresence(); // este es el poll periodico: la senal de presencia principal
    request->send(200, "application/json", ledsBuildStateJson());
  });

  server.on("/api/leds/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
    ledsMarkWebPresence();
    ledsStartScan();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/leds/scanresults", HTTP_GET, [](AsyncWebServerRequest *request) {
    ledsMarkWebPresence();
    StaticJsonDocument<1024> doc;
    {
      // g_scanRunning/g_scanResults los escribe la tarea de escaneo
      // (nucleo 0): hay que leerlos bajo el mismo mutex.
      LedsMutexGuard guard(g_scanMutex);
      doc["running"] = g_scanRunning;
      JsonArray results = doc.createNestedArray("results");
      for (int i = 0; i < g_scanResultCount; i++) {
        JsonObject o = results.createNestedObject();
        o["mac"] = g_scanResults[i].mac;
        o["name"] = g_scanResults[i].name;
        o["rssi"] = g_scanResults[i].rssi;
      }
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Comandos: se acumula el cuerpo (puede llegar en varios paquetes) y se
  // procesa entero cuando se ha recibido por completo. Patron estandar y
  // reutilizable para cualquier endpoint JSON sobre ESPAsyncWebServer.
  server.on("/api/leds/command", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // No se responde aqui: con ESPAsyncWebServer este callback se
      // dispara nada mas llegar las cabeceras, antes de que el body (y
      // por tanto ledsHandleCommand) se haya procesado. Si respondemos
      // aqui, el propio cliente que envia el comando puede recibir el
      // "ok" y refrescar el estado ANTES de que se haya aplicado de
      // verdad. La respuesta real se envia al final del onBody de abajo.
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;
      if (index == 0) body = "";
      body += String((char*)data).substring(0, len);
      if (index + len == total) {
        ledsMarkWebPresence();
        ledsHandleCommand(body);
        request->send(200, "application/json", "{\"ok\":true}");
      }
    });
}

// ============================================================================
// ------------------------------- API publica del modulo -----------------------
// ============================================================================

void ledsInit() {
  Serial.println("[LEDS] Inicializando modulo de tiras LED BLE...");
  // IMPORTANTE - limite de conexiones BLE simultaneas:
  // Este firmware nunca abre mas de LEDS_MAX_CONCURRENT_CONNECTIONS (3)
  // conexiones BLE a la vez, porque la libreria NimBLE-Arduino reserva
  // ese numero de "slots" en tiempo de compilacion (por defecto 3) y
  // pasarse de ese numero corrompe la pila BLE y reinicia el ESP32.
  // Para tener mas tiras conectadas SIMULTANEAMENTE, hay que subir ese
  // limite en la propia libreria (no se puede desde este .cpp):
  //   1. Localiza el archivo de tu instalacion de Arduino:
  //      libraries/NimBLE-Arduino/src/nimconfig.h
  //   2. Busca la linea "#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3"
  //      y sube el numero (p.ej. a 6, para cubrir LEDS_MAX_STRIPS).
  //   3. Sube tambien LEDS_MAX_CONCURRENT_CONNECTIONS en este archivo al
  //      MISMO numero, y vuelve a compilar/flashear.

  g_scanMutex = xSemaphoreCreateMutex();
  g_bleMutex  = xSemaphoreCreateMutex();

  ledsLoadGroup();
  ledsLoadSettings();
  ledsLoadPrograms();
  g_scheduleForcedState = g_power;

  NimBLEDevice::init("");
  // MTU moderado: suficiente para los comandos de 7-9 bytes de este
  // protocolo, sin reservar memoria de mas.
  NimBLEDevice::setMTU(64);

  ledsRegisterWebRoutes();

  // Tarea de reconexion BLE, separada de loop() (ver ledsReconnectTaskFunc).
  // Stack 8192, NO 4096: esta tarea hace connect()+getService()+
  // getCharacteristic() (operaciones GATT reales, no solo escanear). Con
  // 4096 el stack puede desbordarse de forma silenciosa (sin panic
  // completo del sistema) y la conexion nunca llega a completarse aunque
  // el resto del ESP32 siga respondiendo con normalidad.
  xTaskCreatePinnedToCore(ledsReconnectTaskFunc, "ledsReconnect", 8192, nullptr, 1, nullptr, 0);

  Serial.printf("[LEDS] Grupo cargado: %d tira(s), color inicial (%d,%d,%d), brillo %d%%\n",
    [](){ int c=0; for(int i=0;i<LEDS_MAX_STRIPS;i++) if(g_strips[i].used) c++; return c; }(),
    g_colorR, g_colorG, g_colorB, g_brightness);
}

// Tarea dedicada a la reconexion BLE (nucleo 0), separada por completo de
// loop()/WebServer/AsyncTCP (nucleo 1). Antes esto se llamaba desde
// loop(): un connect() lento o sin respuesta bloqueaba TODO el sistema
// (WiFi, watchdog, web) durante segundos, provocando cuelgues/reinicios.
static void ledsReconnectTaskFunc(void* pvParameters) {
  for (;;) {
    if (ledsBleAllowedNow()) {
      unsigned long now = millis();
      for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
        if (g_strips[i].used && !g_strips[i].connected && now >= g_strips[i].nextRetryMs) {
          ledsTryConnectStrip(g_strips[i]);
          break; // una tira por vuelta, igual que antes
        }
      }
    } else {
      // Nadie viendo la pagina y ningun programa horario pendiente:
      // liberamos las conexiones BLE activas en vez de mantenerlas sin
      // necesidad (una por vuelta, mismo patron que la reconexion).
      for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
        if (g_strips[i].used && g_strips[i].connected && g_strips[i].client != nullptr) {
          LedsMutexGuard guard(g_bleMutex);
          g_strips[i].client->disconnect();
          break;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void ledsLoop() {
  unsigned long now = millis();

  // --- Motor de efectos (no bloqueante, respeta la velocidad configurada) ---
  ledsStepEffect();

  // --- Programa horario ---
  ledsApplySchedule();
}