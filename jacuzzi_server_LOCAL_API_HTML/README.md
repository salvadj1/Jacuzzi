# Servidor de historico del jacuzzi (FastAPI + SQLite)

Este servidor sustituye al antiguo almacenamiento en el ESP32 (buffer
circular en RAM/NVS, limitado a ~7-10 dias). Ahora el historico se guarda
aqui, en un PC con Windows que esté encendido 24/7 y en la misma red
local que el ESP32. El ESP32 **sigue siendo el que controla el jacuzzi y
sirve su propia web de control**: este servidor solo guarda y consulta
datos.

> Nota importante: `python -m http.server` (el servidor de ficheros
> estaticos que trae Python) **no sirve para esto** — no entiende JSON,
> POST ni rutas dinamicas. Hace falta `uvicorn`, que arranca la
> aplicacion FastAPI de verdad (`main.py`). Ya viene incluido en
> `requirements.txt`.

## 1. Arrancar el servidor (primera vez y siguientes)

Solo hace falta tener **Python 3 instalado** (marca "Add python.exe to
PATH" durante la instalación: https://www.python.org/downloads/).

Doble clic en `start_server.bat`. La primera vez crea el entorno virtual
e instala automáticamente todo lo de `requirements.txt`; las siguientes
veces arranca directo, y solo reinstala si `requirements.txt` cambia.

Comprueba que responde: abre `http://IP_DEL_PC:8000/api/health` en el navegador.

## 2. Configuración

Por defecto usa la clave de API `cambia-esta-clave` — **cámbiala** antes
de dejarlo en marcha, y usa la MISMA clave en `config.h` del ESP32
(`REMOTE_LOG_API_KEY`). En Windows (PowerShell/CMD):

```bat
set JACUZZI_API_KEY=tu-clave-secreta-aqui
```

(`start_server.bat`, incluido en esta carpeta, ya tiene una línea para
esto — solo edítala con tu clave real).

La base de datos se crea sola la primera vez, en `jacuzzi_server\jacuzzi.db`
(se puede cambiar la ruta con la variable de entorno `JACUZZI_DB_PATH`).

## 3. Arrancar manualmente en otro momento (opcional)

Si prefieres no usar `start_server.bat`, con el entorno ya creado:

```bat
.venv\Scripts\activate
python -m uvicorn main:app --host 0.0.0.0 --port 8000
```

**Para que arranque solo con Windows** (ver punto 5 del documento de
migración: si el PC se reinicia, el servidor debe levantarse sin que
tengas que hacerlo tú a mano):

- Opción sencilla: pon un acceso directo a `start_server.bat` en la
  carpeta de inicio de Windows (`shell:startup`, se escribe en el
  Explorador de archivos) para que arranque al iniciar sesión.
- Opción robusta (incluso sin haber iniciado sesión, y con reinicio
  automático si se cae): instálalo como servicio de Windows con
  [NSSM](https://nssm.cc/) — `nssm install JacuzziServer` y apunta al
  `python.exe` del `.venv` con los argumentos
  `-m uvicorn main:app --host 0.0.0.0 --port 8000`.

## 5. Endpoints

| Método | Ruta                        | Uso                                              |
|--------|-----------------------------|---------------------------------------------------|
| POST   | `/api/data`                 | El ESP32 guarda una muestra (requiere `X-API-Key`) |
| GET    | `/api/history`               | Historico crudo (por defecto, últimos 7 días)     |
| GET    | `/api/history/agregado`      | Historico agregado por hora/día (vistas largas)   |
| POST   | `/api/history/deleteday`     | Borra un rango `[from, to)` (requiere `X-API-Key`) |
| POST   | `/api/history/format`        | Borra TODO el histórico (requiere `X-API-Key`)     |
| POST   | `/api/diag`                  | El ESP32 guarda una muestra de diagnóstico (requiere `X-API-Key`) |
| GET    | `/api/diag`                  | Historico de diagnóstico (heap, WiFi, reinicios...) |
| POST   | `/api/diag/clear`            | Borra TODO el histórico de diagnóstico (requiere `X-API-Key`) |
| GET    | `/`                          | Panel HTML de solo lectura con los datos crudos   |
| GET    | `/api/health`                | Comprobación de que el servidor está vivo         |

`GET /api/history` devuelve exactamente el mismo formato JSON que
generaba antes el propio ESP32 (`{"samples":[[ts,t1,t2,flags], ...]}`),
así que la página `/datos` del ESP32 no necesita cambios en su
JavaScript: solo cambia de dónde saca los datos el propio ESP32.

## 6. Acceso desde fuera de casa (opcional)

Este servidor **no está pensado para exponerse directamente a
internet**. Si quieres consultar el panel (`/`) desde fuera:

1. Instala [Tailscale](https://tailscale.com) en el PC (gratis).
2. Activa [Funnel](https://tailscale.com/kb/1223/funnel) sobre el puerto 8000.
3. Accede desde el nombre `https://tu-pc.tu-tailnet.ts.net` que te da Tailscale.

El ESP32 **no** necesita Tailscale: sigue hablando con el servidor por
IP local normal, dentro de la misma red WiFi.

---

# Cambios en el firmware del ESP32 (resumen)

- `config.h`: nuevas constantes `REMOTE_LOG_*` — ajusta
  `REMOTE_LOG_SERVER_URL` a la IP real del PC (ej.
  `"http://192.168.1.50:8000"`) y `REMOTE_LOG_API_KEY` a la misma clave
  que uses aquí (`JACUZZI_API_KEY`).
- `datalog.h` / `datalog.cpp`: reescritos para hablar con este servidor
  por HTTP en vez de guardar en NVS. La interfaz pública
  (`datalogInit`, `datalogLoop`, `datalogToJson`, `datalogDeleteRange`,
  `datalogFormat`) es la MISMA de antes, así que **no ha hecho falta
  tocar `web_server.cpp` ni `esp32_jacuzzi.ino`**.
- `diaglog.h` / `diaglog.cpp` (registro de diagnóstico/salud del
  sistema): migrado con el mismo patrón que `datalog`, a los endpoints
  `/api/diag*` de este mismo servidor. Libera los 6,4 KB que ocupaba su
  buffer NVS (200 entradas), y el historial deja de estar limitado a
  ~16-17 horas. El breadcrumb de memoria RTC y el pico de duración de
  `loop()` **siguen siendo locales a propósito** (no pueden depender de
  la red para registrar por qué se colgó el firmware).
- Envío de muestras: no bloquea el `loop()` principal — se encolan y una
  tarea FreeRTOS aparte (núcleo 0) hace las peticiones HTTP con timeout
  corto (1.5 s por defecto).
- Si el servidor no responde: las muestras se guardan en un pequeño
  buffer de emergencia en RAM (100 muestras por defecto, sin persistir en
  NVS) y se reintentan automáticamente en cuanto el servidor vuelve.

## Pendiente de decidir/ajustar por ti antes de subir el firmware

- **IP fija del PC** en el router (o usar mDNS: cambiar
  `REMOTE_LOG_SERVER_URL` a `"http://jacuzzi-server.local:8000"` si
  prefieres no depender de una IP fija — requiere que el PC anuncie ese
  nombre, ej. con Avahi/Bonjour).
- **Cambiar la clave de API** por defecto en ambos lados (`config.h` del
  ESP32 y `JACUZZI_API_KEY` del servidor).
- **Copias de seguridad** de `jacuzzi.db` (ej. tarea programada que copie
  el fichero a otra carpeta/disco una vez al día).
