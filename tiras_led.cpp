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
#include <nvs_flash.h>
#include <nvs.h>

// ============================================================================
// ---------------------------- Configuracion --------------------------------
// ============================================================================
#define LEDS_MAX_STRIPS        8     // Tiras maximas en el grupo
#define LEDS_MAX_PROGRAMS      5     // Programas horarios maximos
#define LEDS_SCAN_SECONDS      6     // Duracion de cada escaneo bajo demanda
#define LEDS_RECONNECT_MIN_MS  4000  // Backoff de reconexion: minimo
#define LEDS_RECONNECT_MAX_MS  30000 // Backoff de reconexion: maximo
#define LEDS_RECONNECT_STEP_MS 4000  // Backoff de reconexion: incremento
#define LEDS_CONNECT_TIMEOUT_MS 4000 // Timeout maximo para un intento de conexion BLE
#define LEDS_CONNECT_SETTLE_MS  400  // Margen tras conectar antes de enviar la 1a orden (no bloqueante, ver ledsQueueOrSend)
#define LEDS_TX_GAP_MS          120  // Separacion minima entre 2 comandos BLE seguidos a la misma tira (no bloqueante)
// Limite de conexiones BLE SIMULTANEAS que este firmware intentara abrir.
// OJO: esto NO es solo un capricho de diseno, es una proteccion real: la
// libreria NimBLE-Arduino reserva en tiempo de COMPILACION un numero fijo
// de "slots" de conexion (CONFIG_BT_NIMBLE_MAX_CONNECTIONS, 3 por
// defecto). Intentar una conexion por encima de ese limite corrompe la
// pila BLE y reinicia el ESP32. Este valor DEBE ser <= al configurado en
// nimconfig.h de la libreria (ver comentario en ledsInit()). Se deja en 3
// porque es el valor por defecto de la libreria sin tocar nada; si subes
// ese valor en nimconfig.h, sube tambien esta constante a la vez.
#define LEDS_MAX_CONCURRENT_CONNECTIONS 6

// UUIDs del servicio/caracteristica BLE de las tiras ELK-BLEDOM/MELK/LEDBLE
static const char* LEDS_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
static const char* LEDS_CHAR_UUID    = "0000fff3-0000-1000-8000-00805f9b34fb";

// 21 efectos NATIVOS de hardware que la propia tira ejecuta por si sola
// (documentados por ingenieria inversa del protocolo ELK-BLEDOM/LEDBLE,
// variante de comando 0x07, la que usan estas tiras para efectos aunque
// color/power vayan por la variante 0x00). g_effect = 0 significa "sin
// efecto" (color estatico); g_effect = 1..LEDS_HW_EFFECT_COUNT indexa LEDS_HW_EFFECT_CODES
// (index-1). Reutilizable en cualquier proyecto con esta misma tira.
#define LEDS_HW_EFFECT_COUNT 29
static const uint8_t LEDS_HW_EFFECT_CODES[LEDS_HW_EFFECT_COUNT] = {
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
  0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91,
  0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C
};


// ============================================================================
// ------------------------------ Estructuras ---------------------------------
// ============================================================================

// Una tira emparejada dentro del grupo
struct LedStrip {
  bool     used = false;          // slot ocupado
  String   mac;                   // direccion MAC (identificador persistente)
  String   name;                  // nombre visible (del anuncio BLE, o la MAC si no hay)
  // connected/connecting/removing/dropPending son volatile porque onDisconnect() (tarea interna de
  // NimBLE) y la tarea de reconexion los escriben SIN tomar g_bleMutex (ver politica junto a g_bleMutex).
  volatile bool connected   = false; // conectada ahora mismo
  volatile bool connecting  = false; // connect()/descubrimiento GATT en curso (se hace FUERA del mutex)
  volatile bool removing    = false; // la web pidio quitarla: la baja real la ejecuta la tarea de reconexion
  volatile bool dropPending = false; // onDisconnect pide vaciar su cola de envio (lo hace quien tenga el mutex)
  // --- Sincronizacion con el horario (ver ledsUpdateSyncFlags) ---
  // stateSentGen: ultima "generacion" del horario cuyo estado COMPLETO (color/efecto + power) se
  //               encolo a esta tira; solo lo marca ledsSendCurrentStateToStrip (unica fuente de verdad).
  // syncedGen:    ultima generacion cuyo estado esta CONFIRMADO como entregado (cola vacia).
  uint32_t stateSentGen = 0;
  uint32_t syncedGen    = 0;
  // txFailed: algun write BLE a esta tira ha fallado (ver ledsSendRaw). Impide darla
  // por sincronizada y fuerza el reenvio del estado vigente (ver ledsUpdateSyncFlags).
  bool     txFailed     = false;
  uint8_t  resendCount  = 0;     // reenvios de estado hechos en la generacion actual (tope LEDS_SCHEDULE_MAX_RESENDS)
  unsigned long nextRetryMs = 0;  // millis() en el que se reintentara conectar
  unsigned long retryDelayMs = LEDS_RECONNECT_MIN_MS; // backoff actual
  NimBLEClient* client = nullptr;             // cliente NimBLE de esta tira
  NimBLERemoteCharacteristic* writeChar = nullptr; // caracteristica de escritura

  // --- Cola de envio no bloqueante (ver ledsQueueOrSend/ledsFlushPendingTx) ---
  // Permite espaciar 2 comandos seguidos (ej: power luego color) y dar un
  // margen tras conectar, SIN usar delay() en ningun momento: el propio
  // ledsLoop() va vaciando la cola cuando toca, en cada vuelta.
  unsigned long nextTxAllowedMs = 0;   // no se envia nada a esta tira antes de este instante
  bool    pendingCmd  = false;         // hay un 1er comando en cola
  uint8_t pendingData[9] = {0};
  uint8_t pendingLen  = 0;
  String  pendingDesc;                 // descripcion legible del 1er comando en cola (para el log)
  bool    pendingCmd2 = false;         // hay un 2o comando en cola (ej: color tras power)
  uint8_t pendingData2[9] = {0};
  uint8_t pendingLen2 = 0;
  String  pendingDesc2;                // descripcion legible del 2o comando en cola
};

// Un programa horario de encendido/apagado del grupo de LEDs
struct LedProgram {
  bool    enabled     = false;
  uint8_t startHour   = 20;
  uint8_t startMinute = 0;
  uint8_t endHour     = 23;
  uint8_t endMinute   = 0;
  bool    days[7]     = {false,false,false,false,false,false,false}; // 0=Domingo..6=Sabado
  uint8_t colorR      = 255; // color propio del programa (no el global)
  uint8_t colorG      = 120;
  uint8_t colorB      = 0;
  uint8_t intensity   = 100; // brillo propio del programa (0-100 %)
};

// ============================================================================
// ------------------------------ Estado global -------------------------------
// (privado a este .cpp: nada se expone fuera salvo ledsInit()/ledsLoop())
// ============================================================================
static LedStrip    g_strips[LEDS_MAX_STRIPS];
static LedProgram  g_programs[LEDS_MAX_PROGRAMS];
static uint8_t     g_visiblePrograms = 1; // programas visibles en la web (1..LEDS_MAX_PROGRAMS), compartido entre dispositivos y guardado en NVS

static uint8_t  g_colorR = 255, g_colorG = 120, g_colorB = 0; // color base actual
static uint8_t  g_brightness = 100;   // 0-100 %

// Estado del reintento de sincronizacion de efecto entre tiras (ver
// ledsApplyEffectToAllStrips/ledsFlushEffectRetry mas abajo). Declarado
// aqui arriba porque ledsApplyColorToAllStrips necesita poder cancelarlo.
static uint8_t       g_effectRetryCode    = 0;
static uint8_t       g_effectRetriesLeft  = 0;
static unsigned long g_effectRetryNextMs  = 0;
// true si un setSpeed no pudo enviarse por tener el mutex BLE ocupado: ledsFlushSpeedRetry() lo
// reenvia desde ledsLoop() en cuanto se pueda (asi la web no muestra una velocidad que la tira no tiene).
static volatile bool g_speedRetryPending = false;
static bool     g_power = false;      // ON/OFF general del grupo
static uint8_t  g_effect = 0;         // 0 = sin efecto (color estatico); 1..LEDS_HW_EFFECT_COUNT = efecto nativo
static uint8_t  g_speed = 50;         // 0-100 %, velocidad nativa del efecto en curso (solo aplica si g_effect>0)

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

// Protege las ESCRITURAS BLE (writeValue), la cola de envio de cada tira
// (pendingCmd*), el alta de tiras y la PUBLICACION del estado de conexion
// (client/writeChar/connected) de LedStrip. Sin esto, la tarea de
// reconexion (nucleo 0), los comandos web (tarea AsyncTCP) y loop() pueden
// tocar la misma tira a la vez, lo que provoca cuelgues y reinicios.
// POLITICA (importante): el mutex se retiene SOLO durante operaciones
// cortas. connect(), getService(), getCharacteristic() y disconnect() se
// ejecutan FUERA del mutex (pueden tardar segundos) y onDisconnect() NUNCA
// lo toma: corre en la tarea interna de NimBLE, y si esperase un mutex
// retenido por un connect() que a su vez espera a NimBLE habria deadlock.
// Quien lo pida desde loop()/AsyncTCP debe usar siempre un timeout finito.
static SemaphoreHandle_t g_bleMutex = nullptr;
// Protege g_prefs (NVS/Preferences): se guarda/carga desde la tarea del
// webserver (comandos), la tarea de reconexion BLE y ledsLoop() (horario),
// y Preferences no es segura para acceso concurrente entre tareas.
static SemaphoreHandle_t g_prefsMutex = nullptr;

// --- Presencia del usuario en la pagina "/leds" ---
// Todo lo relacionado con BLE (escaneo, reconexion, comandos manuales)
// solo debe ocurrir mientras alguien tiene la pagina "/leds" abierta, para
// no generar trafico BLE (ni riesgo de coexistencia con el WiFi) cuando
// nadie la esta viendo. La UNICA excepcion son los programas horarios
// (encendido/apagado automatico), que deben funcionar siempre.
static volatile unsigned long g_lastWebPresenceMs = 0;
// Ventana BLE del horario: tras cada cambio de estado impuesto por un
// programa (encender, apagar o cambiar su color/intensidad) se incrementa
// g_scheduleGen y se abre una ventana en la que SI se permite conectar y
// enviar aunque no haya nadie en la pagina. La ventana NO es de duracion
// fija: se mantiene abierta mientras alguna tira del grupo no haya recibido
// el ultimo estado (syncedGen != g_scheduleGen), y se cierra en cuanto todas
// lo tienen, con un tope total de LEDS_SCHEDULE_SYNC_MAX_MS.
static volatile uint32_t      g_scheduleGen          = 0;     // contador de cambios de estado del horario
static volatile unsigned long g_scheduleSyncStartMs  = 0;     // instante del ultimo cambio (inicio del tope)
static volatile bool          g_scheduleSyncActive   = false; // hay una ventana de sincronizacion en curso

#define LEDS_PRESENCE_TIMEOUT_MS 3000            // ~2 ciclos del poll de la web (1200ms)
#define LEDS_SCHEDULE_SYNC_MAX_MS 300000UL       // tope total (5 min) de la ventana BLE abierta por el horario si alguna tira no llega a sincronizarse
#define LEDS_SCHEDULE_MAX_RESENDS 3              // reenvios maximos del estado del horario a una misma tira por cambio (evita bucles si sus writes fallan siempre)

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

// true si alguna tira del grupo aun no tiene confirmado el ultimo estado
// impuesto por el horario (su syncedGen va por detras de g_scheduleGen).
static bool ledsScheduleSyncPending() {
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].syncedGen != g_scheduleGen) return true;
  }
  return false;
}

// true si la ventana BLE del horario esta abierta: hay una sincronizacion
// en curso, no se ha superado el tope total y todavia falta alguna tira.
static bool ledsScheduleSyncWindowOpen() {
  if (!g_scheduleSyncActive) return false;
  if ((millis() - g_scheduleSyncStartMs) >= LEDS_SCHEDULE_SYNC_MAX_MS) return false;
  return ledsScheduleSyncPending();
}

// true si el modulo tiene permiso para usar el radio BLE ahora mismo:
// o hay alguien en la pagina, o un programa horario acaba de cambiar de
// estado y todavia esta dentro de su ventana para conectar y aplicarlo.
static bool ledsBleAllowedNow() {
  return ledsUserPresent() || ledsScheduleSyncWindowOpen();
}

// Pequena ayuda RAII para no olvidar nunca soltar un mutex, incluso si hay
// un "return" en medio de la seccion critica. Reutilizable en cualquier
// otro modulo que necesite el mismo patron con un SemaphoreHandle_t.
// timeoutTicks por defecto = portMAX_DELAY (espera indefinida, comportamiento
// previo) para no afectar a los usos internos que ya son seguros. Los
// llamantes que pueden ejecutarse en la tarea AsyncTCP (comandos web) deben
// pasar un timeout finito y comprobar locked() para no colgar el servidor
// web si el mutex esta retenido por una operacion BLE atascada.
class LedsMutexGuard {
public:
  explicit LedsMutexGuard(SemaphoreHandle_t m, TickType_t timeoutTicks = portMAX_DELAY) : m_mutex(m) {
    m_locked = m_mutex ? (xSemaphoreTake(m_mutex, timeoutTicks) == pdTRUE) : true;
  }
  ~LedsMutexGuard() {
    if (m_mutex && m_locked) xSemaphoreGive(m_mutex);
  }
  bool locked() const { return m_locked; }
private:
  SemaphoreHandle_t m_mutex;
  bool m_locked;
};

// Timeout para los guards de g_bleMutex tomados desde el camino de comandos
// web (tarea AsyncTCP): si no se consigue en este plazo, se descarta el
// comando en vez de bloquear el servidor web indefinidamente.
#define LEDS_BLE_MUTEX_TIMEOUT_MS 1000
// Espera maxima del mutex BLE desde loop() y desde la tarea de reconexion: corta, porque si esta
// ocupado se reintenta en la vuelta siguiente sin perder nada (la cola de envio se conserva).
#define LEDS_LOOP_MUTEX_TIMEOUT_MS 20
// Tamano maximo aceptado para el cuerpo de un POST a /api/leds/command (el mayor comando real
// ronda los 450 B). Rechaza con 413 cualquier cuerpo mayor en vez de reservar memoria sin limite.
#define LEDS_MAX_BODY_BYTES 1024

// --- Estado de aplicacion del programa horario (para no repetir logs/acciones) ---
static bool g_scheduleForcedState = false; // ultimo estado ON/OFF calculado por el horario (para detectar cambios)
static bool g_scheduleOwnsPower   = false; // true si el power actual (ON) lo puso el horario, no el usuario
static volatile bool g_scheduleSlotActive = false; // hay una franja de programa vigente ahora mismo (con independencia del power manual)
static volatile bool g_scheduleManualOff  = false; // el usuario apago a mano durante la franja vigente: el horario no reenciende hasta que termine
static bool g_scheduleBootResolved = false; // ya se derivo el estado inicial tras arrancar (requiere reloj NTP valido)
static int  g_scheduleActiveIdx    = -1;    // programa cuyo aspecto (color/intensidad) aplico el horario por ultima vez
static uint8_t g_scheduleAppliedR = 0, g_scheduleAppliedG = 0, g_scheduleAppliedB = 0, g_scheduleAppliedInt = 0; // aspecto aplicado (para detectar ediciones en caliente)

// Declaraciones adelantadas (funciones privadas de este archivo)
static void ledsApplyColorToAllStrips(uint8_t r, uint8_t g, uint8_t b);
static void ledsApplyPowerToAllStrips(bool on, const char *source = "?");
static void ledsApplyEffectToAllStrips(uint8_t effectIndex);
static void ledsFlushPendingTx();
static void ledsSaveGroup();
static void ledsLoadGroup();
static void ledsSaveSettings();
static void ledsLoadSettings();
static void ledsSavePrograms();
static void ledsLoadPrograms();
static void ledsTryConnectStrip(LedStrip &s);
static void ledsReconnectTaskFunc(void* pvParameters);
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

// ============================================================================
// -------------------------- Envio de comandos BLE ----------------------------
// ============================================================================

// Envia un comando crudo (array de bytes) a una tira ya conectada.
// Reutilizable para cualquier comando del protocolo ELK-BLEDOM/generico.
// Registra en el monitor serie CADA comando enviado (o el motivo por el
// que no se pudo enviar), con los bytes en hexadecimal, una descripcion
// legible de que hace el comando, y el resultado de la escritura BLE,
// para tener trazabilidad completa ante fallos.
static void ledsSendRaw(LedStrip &s, const uint8_t *data, size_t len, const String &desc) {
  // Copia local del puntero: onDisconnect() (sin mutex) puede poner s.writeChar a nullptr en
  // paralelo, y comprobar y usar el campo por separado dejaria una ventana de desreferencia nula.
  NimBLERemoteCharacteristic *ch = s.writeChar;
  if (!s.connected || ch == nullptr) {
    Serial.printf("[LEDS][BLE-TX] %s: comando descartado (tira no conectada) [%s]\n",
                  s.mac.c_str(), desc.c_str());
    return;
  }
  String hex;
  for (size_t i = 0; i < len; i++) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X ", data[i]);
    hex += buf;
  }
  // "true" = write sin respuesta (write-without-response): mas rapido y
  // es lo que esperan estas tiras; evita bloquear esperando ACK.
  bool ok = ch->writeValue((uint8_t*)data, len, false);
  if (!ok) s.txFailed = true; // el horario lo usara para no darla por sincronizada y reenviar
  Serial.printf("[LEDS][BLE-TX] %s (%s): [ %s] -> %s [%s]\n",
                s.name.c_str(), s.mac.c_str(), hex.c_str(),
                ok ? "OK" : "FALLO", desc.c_str());
}

// Envia YA un comando si esta tira puede recibirlo ahora mismo (ha pasado
// su margen minimo desde el ultimo envio/conexion), o si no, lo deja
// encolado (maximo 2 en cola) para que ledsFlushPendingTx() lo envie mas
// adelante desde ledsLoop(). Nunca bloquea: no hay delay() en ningun caso.
// Reutilizable para cualquier protocolo de tira BLE que necesite espaciar
// sus comandos. "desc" es una descripcion legible del comando (para el
// log), que viaja con el comando aunque quede encolado.
static void ledsQueueOrSend(LedStrip &s, const uint8_t *data, size_t len, const String &desc) {
  if (!s.connected || s.writeChar == nullptr) return;
  if (len > sizeof(s.pendingData)) return; // proteccion, no deberia ocurrir (comandos de 9 bytes)

  if (!s.pendingCmd && millis() >= s.nextTxAllowedMs) {
    // Puede enviarse ya: no hay nada delante en la cola y ha pasado el margen.
    ledsSendRaw(s, data, len, desc);
    s.nextTxAllowedMs = millis() + LEDS_TX_GAP_MS;
  } else if (!s.pendingCmd) {
    s.pendingCmd = true;
    memcpy(s.pendingData, data, len);
    s.pendingLen = (uint8_t)len;
    s.pendingDesc = desc;
  } else {
    // Ya hay un 1er comando en cola: este va de 2o (sustituye a cualquier
    // 2o comando anterior sin enviar, para no acumular ordenes obsoletas
    // ej. varios cambios de color seguidos mientras la cola esta ocupada).
    s.pendingCmd2 = true;
    memcpy(s.pendingData2, data, len);
    s.pendingLen2 = (uint8_t)len;
    s.pendingDesc2 = desc;
  }
}

// Recorre el grupo y envia el siguiente comando en cola de cada tira que
// ya pueda recibirlo (margen cumplido). Se llama en cada vuelta de
// ledsLoop(): es la pieza que hace que todo lo anterior sea no bloqueante.
static void ledsFlushPendingTx() {
  // Timeout FINITO en todos los accesos al mutex: esto corre en loop(), y esperarlo sin limite
  // (portMAX_DELAY) congelaba el firmware entero mientras otra tarea lo retenia. Si no se consigue,
  // se salta esa tira y se reintenta en la vuelta siguiente: la cola no se pierde.
  unsigned long now = millis();
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    LedStrip &s = g_strips[i];
    if (!s.used) continue;

    // onDisconnect() no puede tocar la cola (no toma el mutex): pide vaciarla con dropPending y
    // la vaciamos aqui, con el mutex, para no pisar un envio que este en marcha en otra tarea.
    if (s.dropPending) {
      LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_LOOP_MUTEX_TIMEOUT_MS));
      if (guard.locked()) {
        s.dropPending = false;
        s.pendingCmd  = false;
        s.pendingCmd2 = false;
      }
      continue;
    }

    if (!s.connected || !s.pendingCmd) continue;
    if (now < s.nextTxAllowedMs) continue;

    LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_LOOP_MUTEX_TIMEOUT_MS));
    if (!guard.locked()) continue; // mutex ocupado: se reintenta en la siguiente vuelta
    if (!s.connected || !s.pendingCmd) continue; // pudo cambiar mientras esperabamos el mutex

    ledsSendRaw(s, s.pendingData, s.pendingLen, s.pendingDesc);
    s.nextTxAllowedMs = millis() + LEDS_TX_GAP_MS;
    s.pendingCmd = false;

    if (s.pendingCmd2) {
      // Pasa el 2o comando a 1a posicion: se enviara en la siguiente
      // vuelta en la que ya haya pasado el nuevo margen.
      s.pendingCmd = true;
      memcpy(s.pendingData, s.pendingData2, s.pendingLen2);
      s.pendingLen = s.pendingLen2;
      s.pendingDesc = s.pendingDesc2;
      s.pendingCmd2 = false;
    }
  }
}

// Construye una descripcion legible de un color, para el log: reconoce
// algunos colores habituales por su nombre y si no, muestra el RGB en
// crudo. Reutilizable para cualquier log o interfaz que describa colores.
static String ledsDescribeColor(uint8_t r, uint8_t g, uint8_t b) {
  if (r == 0   && g == 0   && b == 0)   return "color negro/apagado";
  if (r == 255 && g == 255 && b == 255) return "color blanco";
  if (r == 255 && g == 0   && b == 0)   return "color rojo";
  if (r == 0   && g == 255 && b == 0)   return "color verde";
  if (r == 0   && g == 0   && b == 255) return "color azul";
  if (r == 255 && g == 255 && b == 0)   return "color amarillo";
  if (r == 0   && g == 255 && b == 255) return "color cian";
  if (r == 255 && g == 0   && b == 255) return "color magenta";
  char buf[32];
  snprintf(buf, sizeof(buf), "color RGB(%u,%u,%u)", r, g, b);
  return String(buf);
}

// Envia color RGB (ya con el brillo aplicado) a una tira concreta.
static void ledsSendColorToStrip(LedStrip &s, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[9] = {0x7E, 0x07, 0x05, 0x03, r, g, b, 0x10, 0xEF};
  String desc = ledsDescribeColor(r, g, b) + ", brillo " + String(g_brightness) + "%";
  ledsQueueOrSend(s, cmd, sizeof(cmd), desc);
}

// Envia encendido/apagado a una tira concreta.
// "source": DIAGNOSTICO TEMPORAL, identifica quien pidio el envio
// (connect/schedule/comando) para poder ver en el log el origen exacto
// de cada orden y detectar duplicados. Quitar el parametro cuando ya
// no haga falta (o dejarlo, es inocuo y reutilizable para trazas futuras).
static void ledsSendPowerToStrip(LedStrip &s, bool on, const char *source = "?") {
  uint8_t cmd[9] = {0x7E, 0x04, 0x04, (uint8_t)(on ? 0x01 : 0x00), 0x00, 0x00, 0x00, 0x00, 0xEF};
  Serial.printf("[LEDS][NVS] ledsSendPowerToStrip origen=%s on=%d\n", source, on);
  ledsQueueOrSend(s, cmd, sizeof(cmd), on ? "encendido" : "apagado");
}

// Envia el comando NATIVO de brillo (0-100%) a una tira concreta, sin
// tocar el color guardado en la tira. Reutilizable para cualquier tira
// ELK-BLEDOM/LEDBLE que acepte esta variante de comando de brillo.
static void ledsSendBrightnessToStrip(LedStrip &s, uint8_t pct) {
  uint8_t cmd[9] = {0x7E, 0x04, 0x01, pct, 0xFF, 0xFF, 0xFF, 0x00, 0xEF};
  char desc[24];
  snprintf(desc, sizeof(desc), "brillo nativo %u%%", pct);
  ledsQueueOrSend(s, cmd, sizeof(cmd), desc);
}

// Aplica el brillo NATIVO a todas las tiras conectadas del grupo.
static void ledsApplyBrightnessToAllStrips(uint8_t pct) {
  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) {
    Serial.println("[LEDS] Mutex BLE ocupado, brillo descartado");
    return;
  }
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendBrightnessToStrip(g_strips[i], pct);
    }
  }
}

// Aplica un color (con brillo ya escalado) a TODAS las tiras conectadas
// del grupo. Es la funcion que usan tanto los comandos manuales como
// cada paso de los efectos.
static void ledsApplyColorToAllStrips(uint8_t r, uint8_t g, uint8_t b) {
  g_effectRetriesLeft = 0; // un color manual cancela cualquier reintento de sincronizacion de efecto pendiente
  uint8_t sr = ledsScaleChannel(r, g_brightness);
  uint8_t sg = ledsScaleChannel(g, g_brightness);
  uint8_t sb = ledsScaleChannel(b, g_brightness);
  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) {
    Serial.println("[LEDS] Mutex BLE ocupado, color descartado");
    return;
  }
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendColorToStrip(g_strips[i], sr, sg, sb);
    }
  }
}

// Aplica encendido/apagado a todas las tiras conectadas del grupo.
static void ledsApplyPowerToAllStrips(bool on, const char *source) {
  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) {
    Serial.println("[LEDS] Mutex BLE ocupado, power descartado");
    return;
  }
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendPowerToStrip(g_strips[i], on, source);
    }
  }
}

// Envia el comando de efecto NATIVO (variante 0x07 del protocolo) a una
// tira concreta: la propia tira ejecuta la animacion por hardware, sin
// que el ESP32 tenga que enviar pasos intermedios. "code" es uno de los
// valores de LEDS_HW_EFFECT_CODES. Reutilizable para cualquier tira
// ELK-BLEDOM/LEDBLE que acepte esta variante de comando de efectos.
static void ledsSendEffectToStrip(LedStrip &s, uint8_t code) {
  uint8_t cmd[9] = {0x7E, 0x05, 0x03, code, 0x03, 0xFF, 0xFF, 0x00, 0xEF};
  char desc[24];
  snprintf(desc, sizeof(desc), "efecto hw 0x%02X", code);
  ledsQueueOrSend(s, cmd, sizeof(cmd), desc);
}

// Envia la velocidad NATIVA (0-100%) del efecto en curso a una tira
// concreta. Solo tiene efecto visible mientras la tira esta ejecutando
// un efecto de hardware (ver ledsSendEffectToStrip). Reutilizable igual
// que la funcion anterior.
static void ledsSendEffectSpeedToStrip(LedStrip &s, uint8_t speedPct) {
  uint8_t cmd[9] = {0x7E, 0x07, 0x02, speedPct, 0xFF, 0xFF, 0xFF, 0x00, 0xEF};
  char desc[24];
  snprintf(desc, sizeof(desc), "velocidad efecto %u%%", speedPct);
  ledsQueueOrSend(s, cmd, sizeof(cmd), desc);
}

// --- Reintento de sincronizacion de efecto entre tiras -----------------------
// Al aplicar un efecto, el ESP32 solo tiene un radio BLE y debe repartir el
// tiempo de aire entre todas las conexiones simultaneas: la entrega real de
// cada comando puede llegar en instantes distintos a cada tira, dando un
// efecto visible desincronizado. Reenviar el mismo comando un par de veces
// mas (con un pequeño lapso) imita lo que ocurre al pulsar varias veces a
// mano, y aumenta la probabilidad de que las tiras acaben alineadas.
// Reutilizable para cualquier comando de efecto BLE que necesite este
// refuerzo de sincronizacion no bloqueante.
#define LEDS_EFFECT_RESYNC_RETRIES 2      // reintentos ADICIONALES tras el primer envio
#define LEDS_EFFECT_RESYNC_GAP_MS  300    // lapso entre cada reintento

// Aplica un efecto de hardware (indice 1..LEDS_HW_EFFECT_COUNT) a todas
// las tiras conectadas, seguido de la velocidad actual. effectIndex==0
// no deberia llegar aqui (ver ledsHandleCommand/ledsApplySchedule: el
// caso 0 aplica color estatico en su lugar, via ledsApplyColorToAllStrips).
static void ledsApplyEffectToAllStrips(uint8_t effectIndex) {
  if (effectIndex == 0 || effectIndex > LEDS_HW_EFFECT_COUNT) return;
  uint8_t code = LEDS_HW_EFFECT_CODES[effectIndex - 1];
  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) {
    Serial.println("[LEDS] Mutex BLE ocupado, efecto descartado");
    return;
  }
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendEffectToStrip(g_strips[i], code);
      ledsSendEffectSpeedToStrip(g_strips[i], g_speed);
    }
  }
  // Programa los reintentos de resincronizacion (los cancela y reemplaza
  // si ya habia unos en curso de un efecto/pulsacion anterior).
  g_effectRetryCode   = code;
  g_effectRetriesLeft = LEDS_EFFECT_RESYNC_RETRIES;
  g_effectRetryNextMs = millis() + LEDS_EFFECT_RESYNC_GAP_MS;
}

// Reenvia (no bloqueante) el ultimo comando de efecto aplicado, si toca
// segun el lapso programado. Se llama en cada vuelta de ledsLoop().
static void ledsFlushEffectRetry() {
  if (g_effectRetriesLeft == 0) return;
  if (millis() < g_effectRetryNextMs) return;

  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) return; // se reintentara en la siguiente vuelta, sin perder el contador
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendEffectToStrip(g_strips[i], g_effectRetryCode);
    }
  }
  g_effectRetriesLeft--;
  g_effectRetryNextMs = millis() + LEDS_EFFECT_RESYNC_GAP_MS;
}

// Envia la velocidad NATIVA del efecto (0-100%) a todas las tiras conectadas del grupo.
// Devuelve false si no se pudo tomar el mutex BLE en "timeoutTicks" (no se envia nada), para que
// el llamante decida si reintenta. Reutilizable con cualquier grupo de tiras de este protocolo.
static bool ledsApplySpeedToAllStrips(uint8_t speedPct, TickType_t timeoutTicks) {
  LedsMutexGuard guard(g_bleMutex, timeoutTicks);
  if (!guard.locked()) return false;
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (g_strips[i].used && g_strips[i].connected) {
      ledsSendEffectSpeedToStrip(g_strips[i], speedPct);
    }
  }
  return true;
}

// Reenvia (no bloqueante) una velocidad que quedo pendiente por mutex ocupado (ver setSpeed).
// Se llama en cada vuelta de ledsLoop(). Se anula sola si ya no aplica (apagado o sin efecto).
static void ledsFlushSpeedRetry() {
  if (!g_speedRetryPending) return;
  if (!g_power || g_effect == 0) { g_speedRetryPending = false; return; }
  if (ledsApplySpeedToAllStrips(g_speed, pdMS_TO_TICKS(LEDS_LOOP_MUTEX_TIMEOUT_MS))) {
    g_speedRetryPending = false;
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
  // IMPORTANTE: se ejecuta en la tarea interna de NimBLE y NO debe tomar g_bleMutex (ver la politica
  // junto a g_bleMutex): esperarlo mientras un connect() lo retiene y espera a NimBLE es un deadlock.
  // Solo escribe campos simples (volatile) y delega el vaciado de la cola en ledsFlushPendingTx()
  // mediante dropPending: asi, al reconectar, no se reenvia un comando obsoleto (duplicados).
  void onDisconnect(NimBLEClient* client, int reason) override {
    if (idx < 0 || idx >= LEDS_MAX_STRIPS) return;
    LedStrip &s = g_strips[idx];
    // Cliente obsoleto (tira quitada o slot reasignado): no tocar el estado de la tira actual.
    if (s.client != client) return;
    s.connected   = false;
    s.writeChar   = nullptr;
    s.nextRetryMs = millis() + s.retryDelayMs;
    s.dropPending = true;
    Serial.printf("[LEDS] Tira %s desconectada, reintento en %lums\n",
                  s.mac.c_str(), s.retryDelayMs);
  }
private:
  int idx;
};

// Encola el reenvio del estado VIGENTE del grupo (color/efecto/brillo y
// on-off) a UNA tira, para que quede igual que el resto. Se usa al
// reconectar y cuando el horario detecta que una tira conectada no recibio
// su ultimo estado. Si hay que encender, el color/efecto va primero y el
// opcode de "power ON" despues (orden invertido a proposito: en esta tira el
// opcode de encendido solo no siempre reactiva la salida). Reutilizable con
// cualquier tira de este mismo protocolo. El llamante debe tener g_bleMutex.
static void ledsSendCurrentStateToStrip(LedStrip &s, const char *source) {
  if (g_power) {
    if (g_effect == 0) {
      ledsSendColorToStrip(s, ledsScaleChannel(g_colorR, g_brightness),
                              ledsScaleChannel(g_colorG, g_brightness),
                              ledsScaleChannel(g_colorB, g_brightness));
    } else {
      ledsSendEffectToStrip(s, LEDS_HW_EFFECT_CODES[g_effect - 1]);
      ledsSendEffectSpeedToStrip(s, g_speed);
    }
  }
  ledsSendPowerToStrip(s, g_power, source);
  s.stateSentGen = g_scheduleGen; // el estado vigente (incluido el del horario) queda encolado para esta tira
}

// Cancela un intento de conexion BLE (o libera una tira que quedo a medias): desconecta si hace
// falta, libera el slot de NimBLE (deleteClient) y programa el siguiente reintento con backoff.
// NO necesita g_bleMutex: solo la tarea de reconexion crea/borra clientes y una tira no conectada
// nunca recibe escrituras. Se anula s.client ANTES del delete para que un onDisconnect tardio vea
// un cliente obsoleto y se ignore. Reutilizable con cualquier grupo de clientes NimBLE con backoff.
static void ledsAbortConnect(LedStrip &s, NimBLEClient *client) {
  if (client != nullptr && client->isConnected()) client->disconnect();
  s.connected = false;
  s.writeChar = nullptr;
  s.client    = nullptr;
  if (client != nullptr) NimBLEDevice::deleteClient(client); // libera el slot BLE (evita agotar el pool)
  s.retryDelayMs = min((unsigned long)LEDS_RECONNECT_MAX_MS, s.retryDelayMs + LEDS_RECONNECT_STEP_MS);
  s.nextRetryMs  = millis() + s.retryDelayMs;
  s.connecting   = false;
}

// Intenta conectar (de forma NO bloqueante para el resto del sistema:
// NimBLE hace la conexion en su propia tarea interna, pero la llamada de
// connect() en si tarda hasta unos segundos, por eso solo se invoca cada
// "retryDelayMs" y nunca en cada vuelta del loop) una tira concreta del
// grupo, y localiza su caracteristica de escritura.
static void ledsTryConnectStrip(LedStrip &s) {
  // connect(), getService() y getCharacteristic() pueden tardar segundos y dependen de la tarea
  // interna de NimBLE, asi que se ejecutan FUERA de g_bleMutex (ver politica junto al mutex).
  // Retenerlo durante todo el intento congelaba loop() y la web, y podia provocar un deadlock con
  // onDisconnect(). Tres fases: (1) reservar con mutex, (2) conectar SIN mutex, (3) publicar con mutex.
  NimBLEClient *client = nullptr;
  String mac;

  // ---- FASE 1 (con mutex, breve): comprobaciones, reserva del slot y marca "connecting" ----
  {
    LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
    if (!guard.locked()) return; // se reintentara en la siguiente vuelta de la tarea
    if (!s.used || s.removing || s.connected || s.connecting) return;

    // Proteccion critica: nunca abrir mas conexiones BLE simultaneas de las
    // que la libreria NimBLE tiene reservadas (ver comentario en
    // LEDS_MAX_CONCURRENT_CONNECTIONS). Si ya estamos al limite, no se
    // intenta conectar esta tira ahora: se reintentara mas adelante (por si
    // otra tira se desconecta y libera un hueco), en vez de arriesgarse a
    // reiniciar el ESP32.
    int busyNow = 0;
    for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
      if (g_strips[i].used && (g_strips[i].connected || g_strips[i].connecting)) busyNow++;
    }
    if (busyNow >= LEDS_MAX_CONCURRENT_CONNECTIONS) {
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
    // quedarse colgado con el timeout por defecto de NimBLE (mucho mas largo).
    s.client->setConnectTimeout(LEDS_CONNECT_TIMEOUT_MS);

    client = s.client;
    mac    = s.mac;
    s.connecting = true; // impide un 2o intento y cuenta como conexion ocupada
  }

  // ---- FASE 2 (SIN mutex): conexion y descubrimiento del servicio/caracteristica ----
  // El cliente solo lo crea/borra esta tarea (las bajas las ejecuta ledsProcessPendingRemovals),
  // asi que el puntero local es valido durante toda la fase.
  NimBLEAddress addr(std::string(mac.c_str()), BLE_ADDR_PUBLIC);
  const char *failReason = nullptr;
  NimBLERemoteCharacteristic *ch = nullptr;

  if (!client->connect(addr)) {
    failReason = "fallo al conectar";
  } else {
    NimBLERemoteService *service = client->getService(LEDS_SERVICE_UUID);
    if (service == nullptr) {
      failReason = "conectada pero sin el servicio esperado";
    } else {
      ch = service->getCharacteristic(LEDS_CHAR_UUID);
      if (ch == nullptr) failReason = "conectada pero sin la caracteristica esperada";
    }
    // Si onDisconnect() salto mientras descubriamos, no se puede dar por conectada.
    if (failReason == nullptr && !client->isConnected()) failReason = "se desconecto durante el descubrimiento";
  }

  if (failReason != nullptr) {
    Serial.printf("[LEDS] Tira %s: %s\n", mac.c_str(), failReason);
    ledsAbortConnect(s, client); // libera el slot BLE y programa el reintento con backoff
    return;
  }

  // ---- FASE 3 (con mutex, breve): publicar la conexion y encolar el estado vigente ----
  LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
  if (!guard.locked()) {
    Serial.printf("[LEDS] Tira %s: mutex BLE ocupado al publicar la conexion, se reintentara\n", mac.c_str());
    ledsAbortConnect(s, client);
    return;
  }

  s.writeChar = ch;
  s.connected = true;
  s.connecting = false;
  s.retryDelayMs = LEDS_RECONNECT_MIN_MS; // exito: resetea el backoff
  // Cola limpia antes de encolar el estado: descarta cualquier resto de una conexion anterior.
  s.dropPending = false;
  s.pendingCmd  = false;
  s.pendingCmd2 = false;
  // Margen antes de aceptar la primera orden: esta tira corta la conexion
  // si recibe algo demasiado pronto tras conectar. No es un delay(): solo
  // se fija un instante futuro, y ledsQueueOrSend()/ledsFlushPendingTx()
  // se encargan de esperar a que llegue sin bloquear nada.
  s.nextTxAllowedMs = millis() + LEDS_CONNECT_SETTLE_MS;
  Serial.printf("[LEDS] Tira %s conectada correctamente\n", mac.c_str());

  // Al reconectar, se encola el reenvio del estado actual (color/efecto/
  // brillo/on-off vigentes) para que la tira quede igual que el resto del
  // grupo. Se enviara en cuanto pase el margen de asentamiento, desde
  // ledsLoop(). IMPORTANTE: si hay que encender, el color/efecto va
  // primero y el opcode de "power ON" despues (orden invertido a
  // proposito): en esta tira en concreto el opcode de encendido solo,
  // sin color, no siempre reactiva la salida; el comando de color/efecto
  // si la reactiva de forma fiable.
  ledsSendCurrentStateToStrip(s, "connect");
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
// ------------------------------ Programas horarios ---------------------------
// ============================================================================

// Indica si un programa esta vigente en el instante dado. Reutilizable en
// cualquier proyecto con programas "dias + hora inicio + hora fin".
//   - wday: dia de la semana de hoy (0=Domingo..6=Sabado, como tm_wday).
//   - nowMinutes: minutos desde las 00:00 (0..1439).
// Si inicio < fin el tramo es normal y se comprueba el dia de hoy. Si
// inicio > fin el programa CRUZA medianoche: el tramo previo (>= inicio)
// pertenece al dia de hoy y el tramo posterior (< fin) pertenece al dia en
// que empezo, es decir AYER. Un programa con inicio == fin se ignora.
static bool ledsProgramActiveNow(const LedProgram &p, int wday, int nowMinutes) {
  if (!p.enabled) return false; // un programa desactivado NUNCA actua, aunque el horario coincida
  int startMin = p.startHour * 60 + p.startMinute;
  int endMin   = p.endHour * 60 + p.endMinute;
  if (startMin == endMin) return false; // programa vacio
  if (startMin < endMin) {
    return p.days[wday] && nowMinutes >= startMin && nowMinutes < endMin;
  }
  if (nowMinutes >= startMin) return p.days[wday];        // antes de medianoche: dia de hoy
  if (nowMinutes <  endMin)   return p.days[(wday + 6) % 7]; // despues de medianoche: dia de inicio = ayer
  return false;
}

// Anota que aspecto (color/intensidad) del programa idx es el que esta
// aplicado ahora, SIN enviar nada a las tiras. Sirve para detectar despues
// si el programa se edita en caliente (ver ledsProgramLookChanged).
static void ledsRememberProgramLook(int idx) {
  LedProgram &p = g_programs[idx];
  g_scheduleActiveIdx = idx;
  g_scheduleAppliedR = p.colorR; g_scheduleAppliedG = p.colorG; g_scheduleAppliedB = p.colorB;
  g_scheduleAppliedInt = p.intensity;
}

// true si el programa activo es distinto al aplicado o si su color/
// intensidad se han editado desde la ultima vez que se aplicaron.
static bool ledsProgramLookChanged(int idx) {
  LedProgram &p = g_programs[idx];
  return idx != g_scheduleActiveIdx ||
         p.colorR != g_scheduleAppliedR || p.colorG != g_scheduleAppliedG ||
         p.colorB != g_scheduleAppliedB || p.intensity != g_scheduleAppliedInt;
}

// Aplica el aspecto PROPIO del programa idx: color e intensidad, y fuerza
// color estatico (g_effect = 0) para que un efecto de hardware activo no
// impida que se vea el color del programa, ni en las tiras conectadas ni en
// las que conecten despues (ledsTryConnectStrip reenvia el estado vigente).
static void ledsApplyProgramLook(int idx) {
  LedProgram &p = g_programs[idx];
  g_colorR = p.colorR; g_colorG = p.colorG; g_colorB = p.colorB;
  g_brightness = p.intensity;
  g_effect = 0;
  ledsRememberProgramLook(idx);
  ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
  ledsSaveSettings(); // persiste el color/brillo adoptado, por si hay reinicio despues
}

// Marca un nuevo cambio de estado impuesto por el horario y abre la ventana
// BLE de sincronizacion. LLAMAR DESPUES de haber actualizado g_power/color y
// ANTES de enviar a las tiras: asi una tira que conecte a mitad ve la
// generacion nueva junto con el estado nuevo.
static void ledsBeginScheduleSync() {
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    g_strips[i].txFailed = false;   // los fallos viejos no cuentan para este cambio
    g_strips[i].resendCount = 0;    // cada cambio de estado empieza con su propio cupo de reenvios
  }
  g_scheduleGen++;
  g_scheduleSyncStartMs = millis();
  g_scheduleSyncActive  = true;
}

// Confirma tira a tira que el estado del horario se ha entregado (conectada,
// generacion enviada, sin fallos de envio y cola vacia) y cierra la ventana
// BLE en cuanto todas lo tienen o se agota el tope de LEDS_SCHEDULE_SYNC_MAX_MS.
// Si una tira conectada no recibio el ultimo estado (mutex ocupado al
// repartirlo) o un write fallo, se le REENVIA el estado vigente en vez de
// esperar. No bloqueante: se llama en cada vuelta de ledsLoop().
static void ledsUpdateSyncFlags() {
  if (!g_scheduleSyncActive) return;
  unsigned long now = millis();
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    LedStrip &s = g_strips[i];
    if (!s.used || s.syncedGen == g_scheduleGen || !s.connected) continue;
    bool queueFree = !s.pendingCmd && !s.pendingCmd2 && now >= s.nextTxAllowedMs;
    if (!queueFree) continue; // aun hay algo en cola o no ha pasado el margen entre comandos

    if (s.txFailed || s.stateSentGen != g_scheduleGen) {
      // Falto entregar el estado. Cupo agotado: se abandona esta tira hasta el proximo cambio
      // (asi una tira cuyos writes fallan siempre no mantiene el BLE abierto ni llena el log).
      if (s.resendCount >= LEDS_SCHEDULE_MAX_RESENDS) {
        s.txFailed = false;
        s.syncedGen = g_scheduleGen;
        Serial.printf("[LEDS] Tira %s: estado del horario no entregado tras %d reenvios, se abandona hasta el proximo cambio\n",
                      s.mac.c_str(), LEDS_SCHEDULE_MAX_RESENDS);
        continue;
      }
      // Reenvio SIN espera (timeout 0): si un intento de conexion retiene el mutex no se
      // bloquea el loop(), simplemente se reintenta en la vuelta siguiente.
      LedsMutexGuard guard(g_bleMutex, 0);
      if (guard.locked()) {
        s.txFailed = false;
        s.resendCount++;
        ledsSendCurrentStateToStrip(s, "schedule-resync");
        Serial.printf("[LEDS] Reenvio de estado del horario a %s (%d/%d)\n", s.mac.c_str(), s.resendCount, LEDS_SCHEDULE_MAX_RESENDS);
      }
    } else {
      s.syncedGen = g_scheduleGen; // entregado y cola vacia
    }
  }
  if (!ledsScheduleSyncPending()) {
    g_scheduleSyncActive = false; // todas sincronizadas: el BLE vuelve a cerrarse
  } else if ((now - g_scheduleSyncStartMs) >= LEDS_SCHEDULE_SYNC_MAX_MS) {
    g_scheduleSyncActive = false; // tope agotado: se abandona hasta el proximo cambio o hasta que alguien abra /leds
    Serial.println("[LEDS] Sincronizacion del horario: tope agotado con tiras pendientes");
  }
}

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
  int activeProgramIdx = -1; // programa que gana la franja actual (el primero que coincide)
  for (int i = 0; i < LEDS_MAX_PROGRAMS; i++) {
    if (ledsProgramActiveNow(g_programs[i], wday, nowMinutes)) { shouldBeOn = true; activeProgramIdx = i; break; }
  }
  g_scheduleSlotActive = shouldBeOn;

  // Primera pasada con reloj valido tras arrancar: si estamos dentro de una
  // franja y el grupo ya estaba encendido (guardado en NVS), se asume que
  // lo encendio el horario y se le devuelve la propiedad del power, para que
  // lo apague al terminar la franja. No se reaplica el aspecto (ya esta guardado).
  if (!g_scheduleBootResolved) {
    g_scheduleBootResolved = true;
    if (shouldBeOn && g_power) {
      g_scheduleOwnsPower   = true;
      g_scheduleForcedState = true;
      ledsRememberProgramLook(activeProgramIdx);
      Serial.println("[LEDS] Arranque dentro de una franja con el grupo encendido: el horario retoma el control");
      return;
    }
  }

  // Al terminar la franja se levanta el apagado manual: la siguiente
  // franja volvera a encender con normalidad.
  if (!shouldBeOn) g_scheduleManualOff = false;
  bool effectiveOn = shouldBeOn && !g_scheduleManualOff;

  // Solo actua si cambia respecto al ultimo estado forzado por el
  // programa, para no pisar constantemente un ajuste manual del usuario
  // ni generar trafico BLE innecesario.
  if (effectiveOn != g_scheduleForcedState) {
    g_scheduleForcedState = effectiveOn;

    if (effectiveOn) {
      // El horario ENCIENDE el grupo: esto siempre gana (asegura que el
      // programa se cumpla), y a partir de ahora el horario "es dueno"
      // del power hasta que el o el usuario lo cambien.
      g_power = true;
      g_scheduleOwnsPower = true;
      ledsBeginScheduleSync();
      // Color PRIMERO y power ON despues (ver nota en ledsTryConnectStrip):
      // el color es lo que reactiva la salida de forma fiable en esta tira.
      ledsApplyProgramLook(activeProgramIdx); // color e intensidad PROPIOS del programa, sin efecto
      ledsApplyPowerToAllStrips(true, "schedule-on");
      Serial.println("[LEDS] Programa horario: grupo ENCENDIDO");

    } else if (g_scheduleOwnsPower) {
      // El horario APAGA el grupo, pero SOLO si fue el propio horario
      // quien lo habia encendido. Si el grupo esta encendido porque el
      // usuario lo encendio manualmente (sin ningun programa activo en
      // ese momento), no lo tocamos: el power manual y el del horario
      // quedan desacoplados.
      g_power = false;
      g_scheduleOwnsPower = false;
      g_scheduleActiveIdx = -1;
      ledsBeginScheduleSync();
      ledsApplyPowerToAllStrips(false, "schedule-off");
      ledsSaveSettings(); // persiste el apagado: si no, un reinicio volveria a encender el grupo
      Serial.println("[LEDS] Programa horario: grupo APAGADO");
    }

  } else if (effectiveOn && g_scheduleOwnsPower && ledsProgramLookChanged(activeProgramIdx)) {
    // Cambio en caliente con el grupo ya encendido por el horario: o bien
    // empieza otro programa sin hueco entre medias (franjas consecutivas),
    // o se ha editado el color/intensidad del programa activo.
    ledsBeginScheduleSync();
    ledsApplyProgramLook(activeProgramIdx);
    Serial.println("[LEDS] Programa horario: aspecto actualizado en caliente");
  }
}

// ============================================================================
// --------------------------------- Persistencia -------------------------------
// Namespace propio en NVS ("leds"), totalmente autocontenido: no depende
// de storage.cpp ni comparte claves con el resto del proyecto.
// ============================================================================

static void ledsLoadGroup() {
  LedsMutexGuard guard(g_prefsMutex);
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
  LedsMutexGuard guard(g_prefsMutex);
  g_prefs.begin("leds", false); // lectura/escritura
  int count = 0;
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (!g_strips[i].used || g_strips[i].removing) continue; // una tira en baja no se persiste
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
  LedsMutexGuard guard(g_prefsMutex);
  g_prefs.begin("leds", true);
  g_colorR      = g_prefs.getUChar("r", g_colorR);
  g_colorG      = g_prefs.getUChar("g", g_colorG);
  g_colorB      = g_prefs.getUChar("b", g_colorB);
  g_brightness  = g_prefs.getUChar("bright", g_brightness);
  g_power       = g_prefs.getBool("power", g_power);
  g_effect      = g_prefs.getUChar("effect", g_effect);
  g_speed       = g_prefs.getUChar("speed", g_speed);
  g_prefs.end();
}

static void ledsSaveSettings() {
  LedsMutexGuard guard(g_prefsMutex);
  g_prefs.begin("leds", false);
  // DIAGNOSTICO TEMPORAL: putUChar/putBool devuelven el numero de bytes
  // escritos (0 = fallo, p.ej. NVS sin espacio). Antes no se comprobaba
  // nada, así que un fallo aqui era invisible. Se puede quitar este
  // bloque de comprobacion en cuanto se confirme la causa real.
  size_t wr;
  wr = g_prefs.putUChar("r", g_colorR);      if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'r'");
  wr = g_prefs.putUChar("g", g_colorG);      if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'g'");
  wr = g_prefs.putUChar("b", g_colorB);      if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'b'");
  wr = g_prefs.putUChar("bright", g_brightness); if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'bright'");
  wr = g_prefs.putBool("power", g_power);    if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'power'");
  wr = g_prefs.putUChar("effect", g_effect); if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'effect'");
  wr = g_prefs.putUChar("speed", g_speed);   if (wr == 0) Serial.println("[LEDS][NVS] FALLO al guardar 'speed'");
  g_prefs.end();
  Serial.printf("[LEDS][NVS] Guardado solicitado: power=%d color=(%d,%d,%d) brillo=%d\n",
                g_power, g_colorR, g_colorG, g_colorB, g_brightness);
}

static void ledsLoadPrograms() {
  LedsMutexGuard guard(g_prefsMutex);
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
    g_programs[i].colorR    = g_prefs.getUChar((pfx + "cr").c_str(), g_programs[i].colorR);
    g_programs[i].colorG    = g_prefs.getUChar((pfx + "cg").c_str(), g_programs[i].colorG);
    g_programs[i].colorB    = g_prefs.getUChar((pfx + "cb").c_str(), g_programs[i].colorB);
    g_programs[i].intensity = g_prefs.getUChar((pfx + "in").c_str(), g_programs[i].intensity);
  }
  // Programas visibles en la web. Si la clave no existe (primer arranque tras
  // actualizar) se infiere del ultimo programa activado, para no ocultar
  // programas que ya funcionaban. Nunca baja del ultimo programa activado.
  uint8_t vis = g_prefs.getUChar("progvis", 0);
  uint8_t minVis = 1;
  for (int i = LEDS_MAX_PROGRAMS - 1; i >= 0; i--) {
    if (g_programs[i].enabled) { minVis = i + 1; break; }
  }
  if (vis < minVis) vis = minVis;
  g_visiblePrograms = constrain((int)vis, 1, LEDS_MAX_PROGRAMS);
  g_prefs.end();
}

static void ledsSavePrograms() {
  LedsMutexGuard guard(g_prefsMutex);
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
    g_prefs.putUChar((pfx + "cr").c_str(), g_programs[i].colorR);
    g_prefs.putUChar((pfx + "cg").c_str(), g_programs[i].colorG);
    g_prefs.putUChar((pfx + "cb").c_str(), g_programs[i].colorB);
    g_prefs.putUChar((pfx + "in").c_str(), g_programs[i].intensity);
  }
  g_prefs.putUChar("progvis", g_visiblePrograms);
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
  --amber:#ffb020; --green:#2eff7a; --red:#ff3b2e; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;display:flex;justify-content:space-between;align-items:center;}
a.back{color:var(--dim);text-decoration:none;font-size:11px;letter-spacing:1px;border:1px solid var(--line);padding:5px 10px;border-radius:6px;}
h2{font-size:11px;letter-spacing:1.5px;color:var(--dim);text-transform:uppercase;margin:14px 0 6px 4px;}
.wrap{max-width:560px;margin:0 auto;}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px;margin-top:8px;}
.row{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px;}
button{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;padding:6px 8px;font-family:var(--mono);font-size:11px;cursor:pointer;}
button.active{border-color:var(--amber);color:var(--amber);}
button.on{border-color:var(--green);color:var(--green);}
input[type=range]{width:100%;}
input[type=color]{width:44px;height:30px;border:1px solid var(--line);border-radius:4px;background:none;padding:0;}
input[type=time]{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;font-family:var(--mono);padding:3px 6px;}
select{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;font-family:var(--mono);padding:3px 6px;font-size:11px;}
.progrow{display:flex;align-items:center;gap:6px;}
.progrow label{display:flex;align-items:center;gap:4px;font-size:11px;color:var(--dim);}
.swatch{width:28px;height:28px;border-radius:4px;border:1px solid var(--line);cursor:pointer;}
.stripitem{display:flex;justify-content:space-between;align-items:center;padding:6px;border-bottom:1px solid var(--line);font-size:12px;}
.dim{color:var(--dim);font-size:11px;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:4px;background:#555;}
.dot.offline{background:var(--red);}
.dot-conn{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:4px;background:var(--green);}
.dot-conn.offline{background:var(--red);animation:blinkdot 1s infinite;}
.dot-color{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:6px;background:#555;border:1px solid var(--line);}
@keyframes blinkdot{0%,100%{opacity:1;}50%{opacity:0.2;}}
.daybtn{width:30px;height:26px;font-size:10px;}
.progpanel{margin-top:6px;}
.progtoprow{display:flex;justify-content:space-between;align-items:center;gap:8px;padding-bottom:8px;border-bottom:1px solid var(--line);}
.progtoprow .progleft{display:flex;align-items:center;gap:8px;min-width:0;}
.progtoprow .progleft label{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--dim);}
.progtoprow .progtitle{font-size:13px;font-weight:bold;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.progstatus.on{color:var(--green);}
.progstatus.off{color:var(--dim);}
.progrow2{display:grid;grid-template-columns:50% 50%;gap:8px;margin-top:8px;align-items:center;min-width:0;}
.progdays{display:flex;gap:3px;min-width:0;}
.progdays .daybtn{flex:1;width:auto;min-width:0;padding:0 2px;}
.progtimes{display:flex;gap:6px;min-width:0;}
.progtimes input[type=time]{flex:1;min-width:0;padding:3px 4px;}
.progrow3{display:grid;grid-template-columns:25% 1fr;gap:8px;margin-top:8px;align-items:center;min-width:0;}
.progrow3 input[type=color]{width:100%;height:32px;padding:0;}
.progbright{display:flex;align-items:center;gap:6px;min-width:0;}
.progbright input[type=range]{flex:1;min-width:0;}
.trashbtn{background:none;border:1px solid var(--line);border-radius:4px;width:28px;height:28px;font-size:14px;line-height:1;cursor:pointer;color:var(--red);flex-shrink:0;}
.clonebtn{width:100%;margin-top:6px;}
.effgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(125px,1fr));gap:10px;}
.effcard{border:1px solid var(--line);border-radius:6px;padding:8px;}
.effcatlabel{font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:6px;}
.effpreview{width:100%;height:44px;border-radius:4px;margin-bottom:6px;}
.effthumbs{display:flex;gap:5px;flex-wrap:wrap;}
.effthumb{width:22px;height:22px;border-radius:4px;border:1px solid var(--line);cursor:pointer;}
.effthumb.sel{outline:2px solid var(--dim);outline-offset:1px;}
.colorpicker-btn{width:28px;height:28px;border-radius:4px;border:1px solid var(--line);position:relative;cursor:pointer;background:conic-gradient(red,yellow,lime,cyan,blue,magenta,red);}
.colorpicker-btn input[type=color]{position:absolute;inset:0;opacity:0;width:100%;height:100%;cursor:pointer;padding:0;}
.colorpreview{width:28px;height:28px;border-radius:4px;border:1px solid var(--line);}
</style>
</head>
<body>
<div class="wrap">
<h1>&#9679; INTERFAZ LEDS <a class="back" href="/">&larr; VOLVER</a></h1>

<div class="panel">
  <h2>Tiras emparejadas</h2>
  <div id="stripList"></div>
  <div class="row">
    <button id="btnScan">BUSCAR TIRAS</button>
  </div>
  <div id="scanResults"></div>
</div>

<div class="panel">
  <h2>Encendido</h2>
  <div class="row">
    <button id="btnPower">ENCENDER / APAGAR</button>
    <div class="colorpreview" id="colorPreview"></div>
  </div>
</div>

<div class="panel">
  <h2>Colores</h2>
  <div class="row" id="presets"></div>
  <div class="dim" style="margin-top:8px;">Brillo</div>
  <input type="range" id="brightness" min="0" max="100" value="100">
</div>

<div class="panel">
  <h2>Efectos</h2>
  <div class="row" style="margin-bottom:8px;">
    <button id="btnNoEffect">NINGUNO (color fijo)</button>
  </div>
  <div class="effgrid" id="effects"></div>
  <div class="dim" style="margin-top:10px;">Velocidad</div>
  <input type="range" id="speed" min="0" max="100" value="50">
</div>

<div class="panel">
  <h2>Programas</h2>
  <div id="programList"></div>
</div>

</div>
<script>
const el = id => document.getElementById(id);
// Efectos NATIVOS que ejecuta la propia tira por hardware (indice 0 =
// "Ninguno", color estatico). El indice de este array coincide con
// Efectos NATIVOS que ejecuta la propia tira por hardware. El indice de
// EFFECTS_FLAT (1..LEDS_HW_EFFECT_COUNT) coincide con state.effect (ver
// g_effect / LEDS_HW_EFFECT_CODES en el backend). Agrupados por tipo
// (salto / crossfade / blink) solo para la interfaz; "colors" es la
// aproximacion visual usada en el preview animado, no afecta al hardware.
const EFF_CATS = [
  { key:'estatico', label:'Estaticos', items:[
    {name:'Estatico rojo', colors:['#ff0000']},
    {name:'Estatico azul', colors:['#0000ff']},
    {name:'Estatico verde', colors:['#00ff00']},
    {name:'Estatico cian', colors:['#00ffff']},
    {name:'Estatico amarillo', colors:['#ffff00']},
    {name:'Estatico purpura', colors:['#aa00ff']},
    {name:'Estatico blanco', colors:['#ffffff']},
  ]},
  { key:'salto', label:'Saltos', items:[
    {name:'Salto RGB', colors:['#ff0000','#00ff00','#0000ff']},
    {name:'Salto multicolor', colors:['#ff0000','#00ff00','#0000ff','#ffff00','#ff00ff','#00ffff']},
  ]},
  { key:'cross', label:'Crossfade', items:[
    {name:'Crossfade RGB', colors:['#ff0000','#00ff00','#0000ff']},
    {name:'Crossfade multicolor', colors:['#ff0000','#ff7800','#ffff00','#00ff00','#00ffff','#0000ff','#ff00ff']},
    {name:'Crossfade rojo', colors:['#ff0000','#550000']},
    {name:'Crossfade verde', colors:['#00ff00','#003300']},
    {name:'Crossfade azul', colors:['#0000ff','#000055']},
    {name:'Crossfade amarillo', colors:['#ffff00','#333300']},
    {name:'Crossfade cian', colors:['#00ffff','#003333']},
    {name:'Crossfade magenta', colors:['#ff00ff','#330033']},
    {name:'Crossfade blanco', colors:['#ffffff','#444444']},
    {name:'Crossfade rojo+verde', colors:['#ff8800','#331a00']},
    {name:'Crossfade rojo+azul', colors:['#aa00ff','#220033']},
    {name:'Crossfade verde+azul', colors:['#88ff00','#223300']},
  ]},
  { key:'blink', label:'Blink', items:[
    {name:'Blink multicolor', colors:['#ff0000','#00ff00','#0000ff','#ffff00','#ff00ff','#00ffff']},
    {name:'Blink rojo', colors:['#ff0000']},
    {name:'Blink verde', colors:['#00ff00']},
    {name:'Blink azul', colors:['#0000ff']},
    {name:'Blink amarillo', colors:['#ffff00']},
    {name:'Blink cian', colors:['#00ffff']},
    {name:'Blink magenta', colors:['#ff00ff']},
    {name:'Blink blanco', colors:['#ffffff']},
  ]},
];
// EFFECTS_FLAT: mismo orden que LEDS_HW_EFFECT_CODES en el backend, para
// mapear cada item a su indice real 1..22 de state.effect.
const EFFECTS_FLAT = [].concat(...EFF_CATS.map(c => c.items.map(it => Object.assign({cat:c.key, type:c.key}, it))));
EFFECTS_FLAT.forEach((e,i) => e.index = i + 1);

const EFF_ANIM_MS = 5000;
// Tras los primeros EFF_ANIM_MS desde que carga la pagina, las previas no
// seleccionadas dejan de reiniciar su animacion en cada poll de refreshState
// (cada ~1.2s) y se quedan fijas en su color; si no, parpadearian sin parar.
setTimeout(() => { effInitialAnimDone = true; }, EFF_ANIM_MS);
let effSpeedPct = 50;
let effSelectedIdx = {};  // cat.key -> indice dentro de la categoria (para el preview)
let effLastChangedCat = null; // categoria que se dejo en bucle tras seleccionar
let previewedEffectIndex = null; // ultimo efecto animado en colorPreview (evita reiniciar la animacion en cada poll)
let effInitialAnimDone = false; // true tras los primeros EFF_ANIM_MS: las previas no seleccionadas dejan de reiniciar su animacion en cada poll
const effStyleCache = {};

function effCycleDurMs(e) {
  const t = effSpeedPct / 100;
  const min = e.type === 'blink' ? 180 : 260;
  const max = e.type === 'blink' ? 2600 : 6000;
  return Math.round(max - t * (max - min));
}
function effBaseColor(e) {
  return e.type === 'blink'
    ? (e.colors.length > 1 ? 'linear-gradient(90deg,' + e.colors.join(',') + ')' : e.colors[0])
    : e.colors[0];
}
function effEnsureKeyframes(e) {
  const id = 'ekf' + e.colors.join('').replace(/[^a-z0-9]/gi, '') + e.type;
  if (effStyleCache[id]) return id;
  const cols = e.colors;
  let rule;
  if (e.type === 'salto') {
    const steps = cols.map((c, i) => `${Math.round(i / cols.length * 100)}%{background:${c}}`).join('');
    rule = `@keyframes ${id}{${steps}100%{background:${cols[0]}}}`;
  } else if (e.type === 'cross') {
    const steps = cols.concat([cols[0]]).map((c, i, arr) => `${Math.round(i / (arr.length - 1) * 100)}%{background:${c}}`).join('');
    rule = `@keyframes ${id}{${steps}}`;
  } else {
    rule = `@keyframes ${id}{0%,49%{opacity:1}50%,100%{opacity:.1}}`;
  }
  const styleEl = document.createElement('style');
  styleEl.textContent = rule;
  document.head.appendChild(styleEl);
  effStyleCache[id] = true;
  return id;
}
// runEffAnimation: con loop=false anima 4s y se deja fija en el ultimo
// color; con loop=true (categoria seleccionada) anima en bucle indefinido.
function runEffAnimation(elem, e, loop) {
  const id = effEnsureKeyframes(e);
  const cycleMs = effCycleDurMs(e);
  elem.style.animation = 'none';
  void elem.offsetWidth;
  clearTimeout(elem._stopTimer);
  if (loop) {
    elem.style.animation = `${id} ${cycleMs}ms linear infinite`;
    return;
  }
  const iterations = Math.max(1, Math.round(EFF_ANIM_MS / cycleMs));
  elem.style.animation = `${id} ${cycleMs}ms linear ${iterations}`;
  elem._stopTimer = setTimeout(() => {
    elem.style.animation = 'none';
    elem.style.background = effBaseColor(e);
  }, iterations * cycleMs);
}

// updateColorPreview: muestra en la previa junto al boton de encendido el
// efecto activo (animado en bucle, igual que la categoria seleccionada en
// el panel de Efectos) o, si no hay efecto (g_effect==0), el color plano
// actual. state.effect ya se guarda en NVS en el backend igual que color/
// brillo/velocidad, asi que esta previa refleja lo persistido al recargar.
function updateColorPreview(s) {
  const preview = el('colorPreview');
  if (s.effect > 0) {
    const e = EFFECTS_FLAT[s.effect - 1];
    if (previewedEffectIndex !== s.effect) {
      previewedEffectIndex = s.effect;
      runEffAnimation(preview, e, true);
    }
  } else {
    if (previewedEffectIndex !== null) {
      previewedEffectIndex = null;
      preview.style.animation = 'none';
      clearTimeout(preview._stopTimer);
    }
    preview.style.background = rgbToHex(s.r, s.g, s.b);
  }
}

function renderEffects() {
  const stage = el('effects');
  stage.innerHTML = '';
  el('btnNoEffect').className = state.effect === 0 ? 'active' : '';
  EFF_CATS.forEach(cat => {
    const selIdx = effSelectedIdx[cat.key] || 0;
    const e0 = Object.assign({ type: cat.key }, cat.items[selIdx]);

    const card = document.createElement('div');
    card.className = 'effcard';

    const label = document.createElement('div');
    label.className = 'effcatlabel';
    label.textContent = cat.label;
    card.appendChild(label);

    const preview = document.createElement('div');
    preview.className = 'effpreview';
    preview.style.background = effBaseColor(e0);
    card.appendChild(preview);
    if (cat.key === effLastChangedCat) runEffAnimation(preview, e0, true);
    else if (!effInitialAnimDone) runEffAnimation(preview, e0, false);
    else preview.style.background = effBaseColor(e0);

    const thumbRow = document.createElement('div');
    thumbRow.className = 'effthumbs';
    cat.items.forEach((it, i) => {
      const e = Object.assign({ type: cat.key }, it);
      const flatIdx = EFFECTS_FLAT.findIndex(f => f.name === it.name);
      const th = document.createElement('div');
      th.className = 'effthumb' + (i === selIdx && cat.key === effLastChangedCat ? ' sel' : '');
      th.style.background = effBaseColor(e);
      th.title = it.name;
      th.onmouseenter = () => runEffAnimation(th, e, false);
      th.onclick = () => {
        effSelectedIdx[cat.key] = i;
        effLastChangedCat = cat.key;
        post('setEffect', { effect: EFFECTS_FLAT[flatIdx].index });
        renderEffects();
      };
      thumbRow.appendChild(th);
    });
    card.appendChild(thumbRow);
    stage.appendChild(card);
  });
}
el('btnNoEffect').onclick = () => { effLastChangedCat = null; post('setEffect', { effect: 0 }); };
el('speed').addEventListener('input', () => {
  effSpeedPct = parseInt(el('speed').value);
  if (effLastChangedCat !== null) renderEffects();
});
const PRESETS = ["#ff0000","#ff7800","#ffff00","#00ff00","#00ffff","#0000ff","#ff00ff","#ffffff"];
const DAY_LABELS = ["D","L","M","X","J","V","S"];
const DAY_ORDER = [1,2,3,4,5,6,0]; // orden visual L..D (los indices de datos siguen siendo 0 = domingo)
let state = {};

function post(cmd, extra) {
  const body = Object.assign({cmd:cmd}, extra || {});
  fetch('/api/leds/command', {method:'POST', body: JSON.stringify(body)})
    .then(r => r.json()).then(refreshState);
}

function refreshState() {
  fetch('/api/leds/state').then(r => r.json()).then(s => {
    state = s;
    const active = document.activeElement;
    // No pisar el <input type="color"> mientras el usuario lo tiene
    // abierto/enfocado: si no, el navegador cierra el selector nativo en
    // cada refresco (cada 1.2s), pareciendo que "se esconde solo".
    if (active !== el('colorPicker')) el('colorPicker').value = rgbToHex(s.r, s.g, s.b);
    updateColorPreview(s);
    el('brightness').value = s.brightness;
    el('speed').value = s.speed;
    el('btnPower').className = s.power ? 'on' : '';
    renderEffects();
    renderStrips();
    // renderPrograms() hace innerHTML completo: destruye y recrea los
    // <input type="time"/"color"> de cada programa. Si el usuario tiene
    // el foco en uno de esos inputs (p.ej. el selector de hora abierto),
    // no se repinta hasta que salga de ahi. Los botones (clonar, papelera,
    // checkbox) no necesitan este bloqueo: si no, un clic en "clonar" o en
    // la papelera se queda con el foco dentro de programList y el nuevo
    // programa no aparece hasta el siguiente refresco manual.
    const activeIsProgInput = el('programList').contains(active) &&
      (active.tagName === 'INPUT' && (active.type === 'time' || active.type === 'color'));
    if (!activeIsProgInput) renderPrograms();
  });
}

function rgbToHex(r,g,b){ return '#' + [r,g,b].map(x=>x.toString(16).padStart(2,'0')).join(''); }
function hexToRgb(hex){ const n=parseInt(hex.slice(1),16); return {r:(n>>16)&255, g:(n>>8)&255, b:n&255}; }

function renderPresets() {
  const pickerHtml = `<div class="colorpicker-btn"><input type="color" id="colorPicker" value="#ff7800"></div>`;
  el('presets').innerHTML = pickerHtml + PRESETS.map(c =>
    `<div class="swatch" style="background:${c}" onclick="applyColor('${c}')"></div>`).join('');
  el('colorPicker').onchange = () => applyColor(el('colorPicker').value);
}
function applyColor(hex) {
  const c = hexToRgb(hex);
  post('setColor', {r:c.r, g:c.g, b:c.b});
}

function renderStrips() {
  if (!state.strips) return;
  el('stripList').innerHTML = state.strips.map((s,i) => {
    // Dos puntos independientes:
    // 1) dot-conn: verde fijo si la tira responde por BLE, rojo
    //    parpadeante si no esta conectada / perdio la conexion.
    // 2) dot-color: color y brillo real que esta mostrando la tira en
    //    este momento (opacidad = brillo actual). Gris si esta apagada
    //    o desconectada. Se actualizan solos junto al resto del estado
    //    (ver setInterval de refreshState).
    const connClass = 'dot-conn' + (!s.connected ? ' offline' : '');
    let colorStyle = '';
    if (s.connected && state.power) {
      const op = Math.max(0, Math.min(100, state.brightness)) / 100;
      colorStyle = `background:rgb(${state.r},${state.g},${state.b});opacity:${op}`;
    }
    return `<div class="stripitem"><span><span class="${connClass}"></span><span class="dot-color" style="${colorStyle}"></span>${s.name} <span class="dim">(${s.mac})</span></span>
     <button onclick="post('removeStrip',{index:${i}})">QUITAR</button></div>`;
  }).join('') || '<div class="dim">Sin tiras emparejadas todavia</div>';
}

// Cuantos programas se muestran actualmente. El backend siempre reserva
// LEDS_MAX_PROGRAMS (5) slots fijos; aqui solo se muestran los visibles
// ("Programa maestro" + los clonados). El numero lo guarda el ESP32
// (state.visiblePrograms), asi todos los dispositivos ven lo mismo.
const MAX_PROGRAMS = 5;
function visibleCount() {
  const v = parseInt(state.visiblePrograms || 1);
  return Math.min(MAX_PROGRAMS, Math.max(1, v));
}

function renderPrograms() {
  if (!state.programs) return;
  const visible = state.programs.slice(0, visibleCount());
  el('programList').innerHTML = visible.map((p,i) => `
    <div class="panel progpanel">
      <div class="progtoprow">
        <div class="progleft">
          <label><input type="checkbox" ${p.enabled?'checked':''} onchange="toggleProgram(${i})"> <span class="progstatus ${p.enabled?'on':'off'}">${p.enabled?'ON':'OFF'}</span></label>
          ${i>0 ? `<button class="trashbtn" title="Eliminar programa" onclick="deleteProgram(${i})">&#128465;</button>` : ''}
        </div>
        <span class="progtitle" ${i===0?'style="color:#ffff00;"':''}>${i===0?'PROGRAMA MAESTRO':'PROGRAMA '+(i+1)}</span>
      </div>
      <div class="progrow2">
        <div class="progdays">
          ${DAY_ORDER.map(dIdx =>
            `<button class="daybtn ${p.days[dIdx]?'active':''}" onclick="toggleProgDay(${i},${dIdx})">${DAY_LABELS[dIdx]}</button>`).join('')}
        </div>
        <div class="progtimes">
          <input type="time" value="${pad(p.startHour)}:${pad(p.startMinute)}" onchange="setProgTime(${i},'start',this.value)">
          <input type="time" value="${pad(p.endHour)}:${pad(p.endMinute)}" onchange="setProgTime(${i},'end',this.value)">
        </div>
      </div>
      ${(p.startHour*60+p.startMinute) > (p.endHour*60+p.endMinute) ? '<div class="dim" style="font-size:11px;margin-top:4px;">Cruza medianoche: los dias marcados son los de inicio</div>' : ''}
      <div class="progrow3">
        <input type="color" value="${rgbToHex(p.colorR,p.colorG,p.colorB)}" onchange="setProgColor(${i},this.value)">
        <div class="progbright">
          <input type="range" min="0" max="100" value="${p.intensity}" onchange="setProgIntensity(${i},this.value)">
          <span class="dim">${p.intensity}%</span>
        </div>
      </div>
    </div>`).join('') +
    (visibleCount() < MAX_PROGRAMS
      ? `<button class="clonebtn" onclick="cloneProgram()">+ CLONAR PROGRAMA</button>`
      : '');
}
function pad(n){ return String(n).padStart(2,'0'); }
function toggleProgram(i){ post('setProgram', {index:i, enabled: !state.programs[i].enabled}); }
// Clona el programa maestro (indice 0) en el siguiente slot libre: copia
// horario, dias, color e intensidad. El slot ya existe en el backend
// (array fijo de 5); al escribirlo, el ESP32 lo marca como visible.
function cloneProgram() {
  if (visibleCount() >= MAX_PROGRAMS) return;
  const src = state.programs[0];
  const newIndex = visibleCount();
  post('setProgram', {
    index: newIndex,
    enabled: src.enabled,
    startHour: src.startHour, startMinute: src.startMinute,
    endHour: src.endHour, endMinute: src.endMinute,
    days: src.days,
    colorR: src.colorR, colorG: src.colorG, colorB: src.colorB,
    intensity: src.intensity
  });
}
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
function setProgColor(i, hex) {
  const c = hexToRgb(hex);
  post('setProgram', {index:i, colorR:c.r, colorG:c.g, colorB:c.b});
}
function setProgIntensity(i, value) {
  post('setProgram', {index:i, intensity: parseInt(value)});
}
// Solo se puede borrar el ultimo programa clonado (mantiene los indices
// visibles siempre consecutivos: maestro + 0..N sin huecos).
function deleteProgram(i) {
  if (i !== visibleCount() - 1) return;
  post('deleteProgram', {index:i});
}

el('btnPower').onclick = () => post('setPower', {power: !state.power});
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
  DynamicJsonDocument doc(3072); // en el heap (vale para ArduinoJson v6 y v7): con 6 tiras y 5 programas se necesitan ~2200 B
  doc["power"]      = g_power;
  doc["visiblePrograms"] = g_visiblePrograms;
  doc["r"] = g_colorR; doc["g"] = g_colorG; doc["b"] = g_colorB;
  doc["brightness"] = g_brightness;
  doc["effect"]     = g_effect;
  doc["speed"]      = g_speed;

  JsonArray strips = doc.createNestedArray("strips");
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    if (!g_strips[i].used || g_strips[i].removing) continue; // las tiras en baja ya no se muestran
    JsonObject o = strips.createNestedObject();
    o["mac"] = g_strips[i].mac;
    o["name"] = g_strips[i].name;
    o["connected"] = (bool)g_strips[i].connected; // cast: el campo es volatile
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
    o["colorR"] = g_programs[i].colorR;
    o["colorG"] = g_programs[i].colorG;
    o["colorB"] = g_programs[i].colorB;
    o["intensity"] = g_programs[i].intensity;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

// Procesa un comando JSON recibido desde la pagina "/leds".
static void ledsHandleCommand(const String &jsonStr) {
  DynamicJsonDocument doc(768); // un setProgram con dias ocupa ~410 B: 512 quedaba justo
  if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
    Serial.println("[LEDS] Comando con JSON invalido, se ignora");
    return;
  }
  String cmd = doc["cmd"] | "";

  if (cmd == "setPower") {
    g_power = doc["power"] | g_power;
    g_scheduleForcedState = g_power; // adopta el estado, evita que el horario lo pise en la siguiente pasada
    g_scheduleOwnsPower = false;     // a partir de ahora el power es manual, no del horario
    // Apagar a mano DENTRO de una franja se respeta hasta que esa franja
    // termine (el horario no reenciende en la vuelta siguiente del loop).
    g_scheduleManualOff = (!g_power && g_scheduleSlotActive);
    // Color/efecto PRIMERO y power despues al encender (ver nota en
    // ledsTryConnectStrip): el color reactiva la salida de forma fiable
    // en esta tira, el opcode de power ON solo no siempre lo hace.
    if (g_power) {
      if (g_effect == 0) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
      else ledsApplyEffectToAllStrips(g_effect);
    } else {
      g_effectRetriesLeft = 0; // apagar cancela cualquier reintento de sincronizacion pendiente
    }
    ledsApplyPowerToAllStrips(g_power, "cmd-setPower");
    ledsSaveSettings();

  } else if (cmd == "setColor") {
    g_colorR = doc["r"] | g_colorR;
    g_colorG = doc["g"] | g_colorG;
    g_colorB = doc["b"] | g_colorB;
    g_effect = 0; // elegir un color manual desactiva el efecto de hardware en curso
    if (g_power) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    ledsSaveSettings();

  } else if (cmd == "setBrightness") {
    g_brightness = constrain((int)(doc["value"] | g_brightness), 0, 100);
    // Brillo nativo: se envia siempre que haya potencia, tanto con color
    // fijo como con efecto de hardware en curso (ya no depende de g_effect).
    if (g_power) ledsApplyBrightnessToAllStrips(g_brightness);
    // Se mantiene tambien el escalado de color como refuerzo cuando no hay
    // efecto activo (compatibilidad si alguna tira ignora el brillo nativo).
    if (g_power && g_effect == 0) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
    ledsSaveSettings();

  } else if (cmd == "setEffect") {
    uint8_t e = doc["effect"] | 0;
    if (e <= LEDS_HW_EFFECT_COUNT) g_effect = e;
    if (g_power) {
      if (g_effect == 0) ledsApplyColorToAllStrips(g_colorR, g_colorG, g_colorB);
      else ledsApplyEffectToAllStrips(g_effect);
    }
    ledsSaveSettings();

  } else if (cmd == "setSpeed") {
    g_speed = constrain((int)(doc["value"] | g_speed), 0, 100);
    // Reenvia la velocidad nativa a las tiras solo si hay un efecto de
    // hardware activo ahora mismo (sin efecto, la velocidad no aplica).
    if (g_power && g_effect > 0) {
      // Timeout FINITO: esto corre en la tarea AsyncTCP y no debe bloquear el servidor web. Si el
      // mutex esta ocupado, el cambio no se pierde: ledsFlushSpeedRetry() lo reenvia desde ledsLoop().
      g_speedRetryPending = !ledsApplySpeedToAllStrips(g_speed, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
      if (g_speedRetryPending) Serial.println("[LEDS] Mutex BLE ocupado, velocidad en reintento");
    }
    ledsSaveSettings();

  } else if (cmd == "addStrip") {
    String mac = doc["mac"] | "";
    String name = doc["name"] | mac;
    if (mac.length() > 0) {
      bool added = false;
      {
        // Con mutex (finito): la tarea de reconexion lee/escribe estos campos en paralelo. El slot
        // se rellena entero y "used" se pone a true AL FINAL, asi nadie ve una tira a medio crear.
        LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
        if (!guard.locked()) {
          Serial.println("[LEDS] Mutex BLE ocupado, alta de tira descartada");
        } else {
          bool duplicated = false;
          for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
            if (g_strips[i].used && !g_strips[i].removing && g_strips[i].mac.equalsIgnoreCase(mac)) duplicated = true;
          }
          if (duplicated) {
            // Dos entradas para la misma MAC abririan dos clientes BLE contra el mismo dispositivo.
            Serial.printf("[LEDS] Tira ya emparejada, se ignora el alta: %s\n", mac.c_str());
          } else {
            for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
              // Slot libre de verdad: sin uso, sin baja pendiente y sin cliente NimBLE asociado.
              if (!g_strips[i].used && !g_strips[i].removing && g_strips[i].client == nullptr) {
                g_strips[i] = LedStrip(); // reinicia el slot
                g_strips[i].mac = mac;
                g_strips[i].name = name;
                g_strips[i].nextRetryMs = millis(); // intenta conectar ya
                g_strips[i].used = true;            // SIEMPRE el ultimo
                added = true;
                Serial.printf("[LEDS] Tira anadida al grupo: %s (%s)\n", name.c_str(), mac.c_str());
                break;
              }
            }
          }
        }
      }
      if (added) ledsSaveGroup(); // NVS fuera del mutex BLE, para no retenerlo durante la escritura
    }

  } else if (cmd == "removeStrip") {
    int index = doc["index"] | -1;
    int seen = -1;
    for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
      if (!g_strips[i].used || g_strips[i].removing) continue; // mismo criterio que el JSON que ve la web
      seen++;
      if (seen == index) {
        // NO se toca el cliente BLE desde aqui (tarea AsyncTCP): desconectar puede tardar y liberar el
        // cliente a mitad de un connect() en curso lo corrompe. Solo se marca la baja; la tarea de
        // reconexion desconecta, hace deleteClient() y libera el slot (ledsProcessPendingRemovals).
        g_strips[i].removing = true;
        ledsSaveGroup(); // persiste ya la baja (ledsSaveGroup ignora las tiras marcadas)
        Serial.printf("[LEDS] Tira marcada para baja: %s\n", g_strips[i].mac.c_str());
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
      if (doc.containsKey("colorR")) p.colorR = doc["colorR"];
      if (doc.containsKey("colorG")) p.colorG = doc["colorG"];
      if (doc.containsKey("colorB")) p.colorB = doc["colorB"];
      if (doc.containsKey("intensity")) p.intensity = constrain((int)doc["intensity"], 0, 100);
      // Escribir en un slot oculto (clonar) lo hace visible: el numero de
      // programas visibles vive en el ESP32 y es el mismo en todos los dispositivos.
      if (index >= g_visiblePrograms) g_visiblePrograms = index + 1;
      ledsSavePrograms();
    }

  } else if (cmd == "deleteProgram") {
    // Borra el programa (vuelve a sus valores por defecto) y lo deja
    // desactivado: si estaba forzando el encendido, deja de hacerlo en
    // la siguiente pasada de ledsApplySchedule().
    int index = doc["index"] | -1;
    if (index >= 0 && index < LEDS_MAX_PROGRAMS) {
      g_programs[index] = LedProgram();
      g_programs[index].enabled = false;
      // Solo se puede borrar el ultimo visible (indices consecutivos): se oculta.
      if (index >= 1 && index == g_visiblePrograms - 1) g_visiblePrograms = index;
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
      // Buffer POR PETICION en request->_tempObject: el propio servidor lo libera al destruir la
      // peticion, incluso si el cliente corta a medias. Antes: un "static String" compartido entre
      // peticiones simultaneas y String((char*)data), que leia fuera del buffer (sin terminador NUL).
      if (index == 0) {
        if (total == 0 || total > LEDS_MAX_BODY_BYTES) {
          request->send(413, "application/json", "{\"ok\":false}");
          return;
        }
        request->_tempObject = malloc(total + 1);
        if (request->_tempObject == nullptr) {
          request->send(500, "application/json", "{\"ok\":false}");
          return;
        }
      }
      char *buf = (char*)request->_tempObject;
      if (buf == nullptr || index + len > total) return; // cuerpo ya rechazado o fragmento incoherente
      memcpy(buf + index, data, len);
      if (index + len == total) {
        buf[total] = '\0';
        ledsMarkWebPresence();
        ledsHandleCommand(String(buf));
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

  g_scanMutex   = xSemaphoreCreateMutex();
  g_bleMutex    = xSemaphoreCreateMutex();
  g_prefsMutex  = xSemaphoreCreateMutex();

  ledsLoadGroup();
  ledsLoadSettings();
  ledsLoadPrograms();
  g_scheduleForcedState = g_power;

  // DIAGNOSTICO TEMPORAL: espacio libre/usado del namespace NVS por
  // defecto ("nvs"), donde vive todo (leds, wifi, datalog, diaglog...).
  // Si "free_entries" esta cerca de 0, los put() empiezan a fallar en
  // silencio (justo el sintoma reportado). Quitar en cuanto se confirme.
  {
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK) {
      Serial.printf("[LEDS][NVS] Particion NVS: total=%d usadas=%d libres=%d namespaces=%d\n",
                    stats.total_entries, stats.used_entries, stats.free_entries, stats.namespace_count);
    } else {
      Serial.println("[LEDS][NVS] No se pudieron leer estadisticas de NVS");
    }
  }

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

// Ejecuta las bajas de tiras pedidas desde la web (removing == true). Se hace aqui, en la tarea de
// reconexion, porque es la UNICA que crea/borra clientes NimBLE: asi nunca se libera un cliente
// mientras otra tarea lo usa. Desconecta FUERA del mutex, libera el slot BLE con deleteClient() (antes
// se perdia: tras unas altas/bajas se agotaban los slots y las tiras dejaban de conectar) y marca el
// slot como libre. Si el mutex esta ocupado se reintenta en la siguiente vuelta (removing sigue activo).
static void ledsProcessPendingRemovals() {
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    LedStrip &s = g_strips[i];
    if (!s.used || !s.removing || s.connecting) continue;

    NimBLEClient *client = s.client;
    if (client != nullptr && client->isConnected()) client->disconnect(); // fuera del mutex

    {
      LedsMutexGuard guard(g_bleMutex, pdMS_TO_TICKS(LEDS_BLE_MUTEX_TIMEOUT_MS));
      if (!guard.locked()) continue; // reintento en la siguiente vuelta
      s.connected   = false;
      s.writeChar   = nullptr;
      s.client      = nullptr; // antes del delete: un onDisconnect tardio vera un cliente obsoleto
      s.pendingCmd  = false;
      s.pendingCmd2 = false;
      s.dropPending = false;
      s.removing    = false;
      s.used        = false;   // el slot queda libre (mac/name se sobrescriben en el proximo alta)
    }
    if (client != nullptr) NimBLEDevice::deleteClient(client); // libera el slot de NimBLE
    Serial.printf("[LEDS] Tira %d eliminada del grupo\n", i);
  }
}

// Red de seguridad: como onDisconnect() ya no toma el mutex y solo escribe flags, si alguna vez una
// desconexion no llegase a notificarse la tira se quedaria marcada como conectada para siempre. Aqui
// se contrasta cada tira "conectada" con el estado real del cliente NimBLE y se corrige si difieren.
static void ledsReconcileConnections() {
  for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
    LedStrip &s = g_strips[i];
    if (!s.used || !s.connected || s.client == nullptr) continue;
    if (s.client->isConnected()) continue;
    s.connected   = false;
    s.writeChar   = nullptr;
    s.dropPending = true; // ledsFlushPendingTx() vaciara su cola
    s.nextRetryMs = millis() + s.retryDelayMs;
    Serial.printf("[LEDS] Tira %s: conexion perdida sin aviso, marcada como desconectada\n", s.mac.c_str());
  }
}

// Tarea dedicada a la reconexion BLE (nucleo 0), separada por completo de
// loop()/WebServer/AsyncTCP (nucleo 1). Antes esto se llamaba desde
// loop(): un connect() lento o sin respuesta bloqueaba TODO el sistema
// (WiFi, watchdog, web) durante segundos, provocando cuelgues/reinicios.
static void ledsReconnectTaskFunc(void* pvParameters) {
  for (;;) {
    ledsProcessPendingRemovals(); // bajas pedidas desde la web (con o sin presencia)
    ledsReconcileConnections();   // corrige tiras "conectadas" cuyo enlace ya no existe

    if (ledsBleAllowedNow()) {
      unsigned long now = millis();
      for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
        if (g_strips[i].used && !g_strips[i].removing && !g_strips[i].connected &&
            now >= g_strips[i].nextRetryMs) {
          ledsTryConnectStrip(g_strips[i]);
          break; // una tira por vuelta, igual que antes
        }
      }
    } else {
      // Nadie viendo la pagina y ningun programa horario pendiente:
      // liberamos las conexiones BLE activas en vez de mantenerlas sin
      // necesidad (una por vuelta, mismo patron que la reconexion).
      // disconnect() FUERA del mutex: puede tardar, y onDisconnect() ya no lo necesita.
      for (int i = 0; i < LEDS_MAX_STRIPS; i++) {
        if (g_strips[i].used && g_strips[i].connected && g_strips[i].client != nullptr) {
          g_strips[i].client->disconnect();
          break;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void ledsLoop() {
  // --- Vacia la cola de comandos BLE pendientes por tira (no bloqueante) ---
  ledsFlushPendingTx();

  // --- Reintentos de resincronizacion de efecto entre tiras ---
  ledsFlushEffectRetry();

  // --- Reintento de velocidad de efecto pendiente por mutex ocupado ---
  ledsFlushSpeedRetry();

  // --- Confirmacion de entrega del estado del horario a cada tira ---
  ledsUpdateSyncFlags();

  // --- Programa horario ---
  ledsApplySchedule();
}