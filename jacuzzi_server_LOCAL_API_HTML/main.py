"""
main.py
-----------------------------------------------------------------------
Servidor de almacenamiento del historico del jacuzzi.

Sustituye al antiguo buffer circular en RAM/NVS del ESP32: el firmware
ahora envia cada muestra por HTTP a este servidor (POST /api/data), y
pide el historico para pintar la grafica de "/datos" tambien por HTTP
(GET /api/history). El ESP32 sigue siendo el unico que controla el
jacuzzi (reles, sensores); este servidor SOLO guarda y sirve datos.

Pensado para correr 24/7 en un PC de la misma red local (ver README.md
de esta carpeta para instrucciones de arranque, systemd, etc.).
-----------------------------------------------------------------------
"""
import os
import sqlite3
import time
from contextlib import contextmanager
from typing import Optional

from fastapi import FastAPI, Header, HTTPException, Query, Request
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel

# ---------------- Configuracion ----------------
# En un entorno real, mueve esto a variables de entorno o a un .env;
# se deja aqui como constantes simples para que el ejemplo sea autocontenido.
API_KEY = os.environ.get("JACUZZI_API_KEY", "cambia-esta-clave")  # debe coincidir con config.h del ESP32
DB_PATH = os.environ.get("JACUZZI_DB_PATH", os.path.join(os.path.dirname(__file__), "jacuzzi.db"))
HISTORY_DEFAULT_DAYS = 7  # mismo comportamiento que el antiguo /api/history del ESP32 ("hasta 7 dias")

# Bits del campo "flags" (deben coincidir EXACTAMENTE con datalog.h del ESP32)
LOG_FLAG_PUMP = 1 << 0
LOG_FLAG_AUTO = 1 << 1
LOG_FLAG_VALVES = 1 << 2
LOG_FLAG_FORCE_SOLAR = 1 << 3

app = FastAPI(title="Jacuzzi Datalog Server")


# ---------------- Base de datos ----------------

@contextmanager
def get_db():
    """Conexion SQLite reutilizable. Modo WAL: mas resistente a cortes de
    luz/apagones a mitad de escritura que el modo por defecto (ver punto 6
    del documento de migracion)."""
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


def init_db():
    with get_db() as conn:
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS samples (
                ts     INTEGER PRIMARY KEY, -- timestamp epoch (segundos). PRIMARY KEY = UNIQUE:
                                             -- un reintento del ESP32 con el mismo ts no duplica
                                             -- (ver punto 3 del documento de migracion).
                t1     REAL NOT NULL,       -- temperatura jacuzzi (C)
                t2     REAL NOT NULL,       -- temperatura solar (C)
                flags  INTEGER NOT NULL
            )
            """
        )
        # Tabla de "rollups" diarios (medias por dia), para servir vistas
        # largas (mes/año) sin tener que agregar miles de filas en cada
        # peticion (ver punto 9 del documento de migracion). Se recalcula
        # bajo demanda en /api/history/agregado; no hace falta un cron
        # aparte para este volumen de datos, pero queda preparado si algun
        # dia se necesita.
        # Tabla del historico de DIAGNOSTICO (salud del sistema: heap, WiFi,
        # motivos de reinicio...). Antes vivia en un buffer NVS de 200
        # entradas en el propio ESP32 (~16-17h de cobertura); ahora, igual
        # que "samples", no tiene limite practico de capacidad.
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS diag_samples (
                ts               INTEGER PRIMARY KEY,
                free_heap        INTEGER NOT NULL,
                min_free_heap    INTEGER NOT NULL,
                max_alloc_heap   INTEGER NOT NULL,
                uptime_sec       INTEGER NOT NULL,
                max_loop_micros  INTEGER NOT NULL,
                min_stack_bytes  INTEGER NOT NULL,
                rssi             INTEGER NOT NULL,
                ws_clients       INTEGER NOT NULL,
                wifi_connected   INTEGER NOT NULL,
                reset_reason     INTEGER NOT NULL,
                reset_reason_text TEXT NOT NULL,
                breadcrumb_text  TEXT NOT NULL,
                wifi_reconnects  INTEGER NOT NULL,
                ntc_errors       INTEGER NOT NULL,
                event_class      INTEGER NOT NULL
            )
            """
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_diag_ts ON diag_samples(ts)")


@app.on_event("startup")
def on_startup():
    init_db()


# ---------------- Autenticacion minima ----------------

def check_api_key(x_api_key: Optional[str]):
    """Token fijo compartido con el ESP32 (ver punto 7 del documento de
    migracion). No es una autenticacion robusta tipo OAuth, pero basta
    para que la API no quede totalmente abierta dentro de la red local."""
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="API key invalida o ausente")


# ---------------- Modelos ----------------

class SampleIn(BaseModel):
    ts: int      # epoch segundos
    t1: float    # temperatura jacuzzi
    t2: float    # temperatura solar
    flags: int


class DiagSampleIn(BaseModel):
    """Una muestra de diagnostico. Los campos de texto (resetReasonText,
    breadcrumbText) y eventClass ya llegan calculados desde el ESP32
    -donde viven los enums de ESP-IDF (esp_reset_reason_t, DiagStage)-,
    asi que este servidor solo los guarda y los devuelve tal cual, sin
    necesitar conocer ese mapeo el mismo."""
    ts: int
    freeHeap: int
    minFreeHeap: int
    maxAllocHeap: int
    uptimeSec: int
    maxLoopMicros: int
    minStackBytes: int
    rssi: int
    wsClients: int
    wifiConnected: int
    resetReason: int
    resetReasonText: str = ""
    breadcrumbText: str = ""
    wifiReconnects: int
    ntcErrors: int
    eventClass: int


# ---------------- Endpoints usados por el ESP32 ----------------

@app.post("/api/data")
def store_sample(sample: SampleIn, x_api_key: Optional[str] = Header(default=None)):
    """Guarda una muestra. Idempotente: si ya existe una con el mismo
    timestamp (reintento del ESP32), se sobreescribe sin duplicar."""
    check_api_key(x_api_key)

    if sample.ts <= 0:
        raise HTTPException(status_code=400, detail="timestamp invalido")

    with get_db() as conn:
        conn.execute(
            "INSERT INTO samples (ts, t1, t2, flags) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(ts) DO UPDATE SET t1=excluded.t1, t2=excluded.t2, flags=excluded.flags",
            (sample.ts, sample.t1, sample.t2, sample.flags),
        )
    return {"ok": True}


@app.get("/api/history")
def get_history(
    desde: Optional[int] = Query(default=None, description="epoch segundos, inclusive"),
    hasta: Optional[int] = Query(default=None, description="epoch segundos, exclusive"),
):
    """Devuelve el historico en el MISMO formato que generaba el ESP32
    antes de la migracion: {"samples":[[ts,t1,t2,flags], ...]}. Asi la
    pagina "/datos" del ESP32 no necesita ningun cambio en su JavaScript.

    Por defecto, si no se pasan fechas, devuelve los ultimos
    HISTORY_DEFAULT_DAYS dias (igual que el comportamiento previo).
    """
    if hasta is None:
        hasta = int(time.time()) + 1
    if desde is None:
        desde = hasta - HISTORY_DEFAULT_DAYS * 86400

    with get_db() as conn:
        rows = conn.execute(
            "SELECT ts, t1, t2, flags FROM samples WHERE ts >= ? AND ts < ? ORDER BY ts ASC",
            (desde, hasta),
        ).fetchall()

    samples = [[r[0], r[1], r[2], r[3]] for r in rows]
    return JSONResponse({"samples": samples})


@app.get("/api/history/agregado")
def get_history_aggregated(
    periodo: str = Query(default="dia", pattern="^(hora|dia)$"),
    desde: Optional[int] = Query(default=None),
    hasta: Optional[int] = Query(default=None),
):
    """Version agregada del historico (media de T1/T2 por hora o por dia),
    pensada para vistas largas (mes/año) sin tener que enviar al ESP32
    miles de muestras crudas (ver seccion 3 del documento de migracion:
    el ESP32 no tiene RAM para construir/parsear eso de golpe).
    """
    if hasta is None:
        hasta = int(time.time()) + 1
    if desde is None:
        desde = hasta - 365 * 86400

    # strftime sobre segundos epoch, agrupando por dia u hora.
    fmt = "%Y-%m-%d" if periodo == "dia" else "%Y-%m-%d %H:00"

    with get_db() as conn:
        rows = conn.execute(
            f"""
            SELECT strftime('{fmt}', ts, 'unixepoch') AS bucket,
                   AVG(t1), AVG(t2), MIN(ts)
            FROM samples
            WHERE ts >= ? AND ts < ?
            GROUP BY bucket
            ORDER BY bucket ASC
            """,
            (desde, hasta),
        ).fetchall()

    puntos = [[r[3], round(r[1], 2), round(r[2], 2)] for r in rows]
    return JSONResponse({"periodo": periodo, "samples": puntos})


@app.post("/api/history/deleteday")
def delete_range(
    from_: int = Query(alias="from"),
    to: int = Query(...),
    x_api_key: Optional[str] = Header(default=None),
):
    """Borra las muestras en [from, to). Mismo contrato que el antiguo
    datalogDeleteRange() del ESP32 (boton 'borrar dia' en /datos)."""
    check_api_key(x_api_key)
    with get_db() as conn:
        conn.execute("DELETE FROM samples WHERE ts >= ? AND ts < ?", (from_, to))
    return {"ok": True}


@app.post("/api/history/format")
def format_all(x_api_key: Optional[str] = Header(default=None)):
    """Borra TODO el historico. Equivalente al antiguo datalogFormat()."""
    check_api_key(x_api_key)
    with get_db() as conn:
        conn.execute("DELETE FROM samples")
    return {"ok": True}


# ---------------- Endpoints de DIAGNOSTICO (salud del sistema) ----------------

@app.post("/api/diag")
def store_diag_sample(sample: DiagSampleIn, x_api_key: Optional[str] = Header(default=None)):
    """Guarda una muestra de diagnostico. Idempotente por timestamp, igual
    que /api/data (un reintento del ESP32 no duplica)."""
    check_api_key(x_api_key)
    if sample.ts < 0:
        raise HTTPException(status_code=400, detail="timestamp invalido")

    with get_db() as conn:
        conn.execute(
            """
            INSERT INTO diag_samples (
                ts, free_heap, min_free_heap, max_alloc_heap, uptime_sec,
                max_loop_micros, min_stack_bytes, rssi, ws_clients,
                wifi_connected, reset_reason, reset_reason_text,
                breadcrumb_text, wifi_reconnects, ntc_errors, event_class
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(ts) DO UPDATE SET
                free_heap=excluded.free_heap, min_free_heap=excluded.min_free_heap,
                max_alloc_heap=excluded.max_alloc_heap, uptime_sec=excluded.uptime_sec,
                max_loop_micros=excluded.max_loop_micros, min_stack_bytes=excluded.min_stack_bytes,
                rssi=excluded.rssi, ws_clients=excluded.ws_clients,
                wifi_connected=excluded.wifi_connected, reset_reason=excluded.reset_reason,
                reset_reason_text=excluded.reset_reason_text, breadcrumb_text=excluded.breadcrumb_text,
                wifi_reconnects=excluded.wifi_reconnects, ntc_errors=excluded.ntc_errors,
                event_class=excluded.event_class
            """,
            (
                sample.ts, sample.freeHeap, sample.minFreeHeap, sample.maxAllocHeap,
                sample.uptimeSec, sample.maxLoopMicros, sample.minStackBytes, sample.rssi,
                sample.wsClients, sample.wifiConnected, sample.resetReason,
                sample.resetReasonText, sample.breadcrumbText, sample.wifiReconnects,
                sample.ntcErrors, sample.eventClass,
            ),
        )
    return {"ok": True}


@app.get("/api/diag")
def get_diag_history(
    desde: Optional[int] = Query(default=None),
    hasta: Optional[int] = Query(default=None),
):
    """Devuelve el historico de diagnostico en el MISMO formato/orden de
    columnas que generaba antes diaglogToJson() en el ESP32, para que
    diagpage.cpp no necesite ningun cambio en su JavaScript. Sin filtro
    de fechas, devuelve TODO (a diferencia de /api/history, aqui no hay
    un limite de "7 dias por defecto": el volumen de diagnostico es mucho
    menor y suele interesar verlo completo)."""
    if hasta is None:
        hasta = int(time.time()) + 1
    if desde is None:
        desde = 0

    with get_db() as conn:
        rows = conn.execute(
            """
            SELECT ts, free_heap, min_free_heap, max_alloc_heap, uptime_sec,
                   max_loop_micros, min_stack_bytes, rssi, ws_clients,
                   wifi_connected, reset_reason, reset_reason_text,
                   breadcrumb_text, wifi_reconnects, ntc_errors, event_class
            FROM diag_samples WHERE ts >= ? AND ts < ? ORDER BY ts ASC
            """,
            (desde, hasta),
        ).fetchall()

    samples = [list(r) for r in rows]
    # intervalMs no tiene sentido en el servidor (es un ajuste del ESP32,
    # persistido alli en NVS); se devuelve a 0 y diagpage.cpp ya lo pide
    # aparte si lo necesita mostrar/editar.
    return JSONResponse({"intervalMs": 0, "samples": samples})


@app.post("/api/diag/clear")
def clear_diag(x_api_key: Optional[str] = Header(default=None)):
    """Borra TODO el historico de diagnostico. Equivalente al antiguo
    diaglogClear()."""
    check_api_key(x_api_key)
    with get_db() as conn:
        conn.execute("DELETE FROM diag_samples")
    return {"ok": True}


# ---------------- Esquema dinamico (para que la web no dependa de columnas fijas) ----------------

# Tablas que el panel de administracion puede listar. Si en el futuro se
# anade una tabla nueva (por ejemplo, otro sensor), basta con incluirla
# aqui: el HTML/JS del panel la detecta sola via /api/schema, sin tocar
# ni una linea de esta seccion ni del frontend.
ADMIN_TABLES = ["samples", "diag_samples"]


def _table_columns(conn: sqlite3.Connection, table: str) -> list[str]:
    """Nombres de columna reales de una tabla, en el orden de la tabla."""
    rows = conn.execute(f"PRAGMA table_info({table})").fetchall()
    return [r[1] for r in rows]


@app.get("/api/schema")
def get_schema():
    """Devuelve, para cada tabla del panel, sus columnas y cuantas de ellas
    son numericas (para que el frontend decida solo que pintar como
    tarjeta/grafica y que pintar como tabla). No requiere API key: es
    metadata, no datos, y la sirve el mismo servidor de solo lectura."""
    with get_db() as conn:
        tablas = {}
        for tabla in ADMIN_TABLES:
            cols = conn.execute(f"PRAGMA table_info({tabla})").fetchall()
            tablas[tabla] = [
                {"nombre": c[1], "tipo": c[2], "es_pk": bool(c[5])}
                for c in cols
            ]
    return JSONResponse({"tablas": tablas})


@app.get("/api/table/{tabla}")
def get_table_rows(
    tabla: str,
    limite: int = Query(default=200, le=2000),
    desde: Optional[int] = Query(default=None, description="epoch segundos, columna ts, inclusive"),
    hasta: Optional[int] = Query(default=None, description="epoch segundos, columna ts, exclusive"),
):
    """Endpoint generico de solo lectura: devuelve filas de cualquier tabla
    del panel (samples o diag_samples) junto con sus columnas, sin que el
    codigo tenga que conocer los nombres de campo de antemano. Esto es lo
    que permite que, si un dia se anaden columnas nuevas a una tabla, el
    panel las muestre sin cambios en este archivo ni en el frontend."""
    if tabla not in ADMIN_TABLES:
        raise HTTPException(status_code=404, detail="tabla no reconocida")

    with get_db() as conn:
        columnas = _table_columns(conn, tabla)
        if hasta is None:
            hasta = int(time.time()) + 1
        if desde is None:
            desde = 0
        rows = conn.execute(
            f"SELECT * FROM {tabla} WHERE ts >= ? AND ts < ? ORDER BY ts DESC LIMIT ?",
            (desde, hasta, limite),
        ).fetchall()

    return JSONResponse({"columnas": columnas, "filas": [list(r) for r in rows]})


# ---------------- Panel de administracion (solo lectura, uso propio) ----------------

@app.get("/", response_class=HTMLResponse)
def admin_page():
    """Panel HTML de solo lectura para consultar los datos crudos desde el
    PC (o desde fuera via Tailscale Funnel). No es el panel de control del
    jacuzzi -eso lo sigue sirviendo el ESP32-, es solo para inspeccionar
    lo que hay guardado.

    A diferencia de la version anterior, este HTML no tiene columnas ni
    nombres de campo escritos a mano: al cargar pide /api/schema para
    saber que columnas tiene cada tabla y /api/table/{{tabla}} para los
    datos, y construye tarjetas + grafica + tabla (o vista de log, si la
    tabla no tiene columnas numericas suficientes) dinamicamente en JS.
    Si en el futuro se anade una columna a samples o diag_samples, o una
    tabla nueva a ADMIN_TABLES, el panel la muestra sola sin tocar este
    archivo."""
    return """
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Jacuzzi - Datos almacenados</title>
<style>
  :root {
    --bg: #0f1115; --panel: #171a21; --card: #1f232c; --border: #2a2f3a;
    --text: #e8e8e8; --muted: #8b93a3; --accent: #3d8bfd;
    --danger: #e5534b; --warning: #d9a441; --success: #3fb950;
  }
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; background: var(--bg); color: var(--text);
         margin: 0; padding: 20px; }
  .wrap { max-width: 900px; margin: 0 auto; }
  h1 { font-size: 18px; font-weight: 500; display: flex; align-items: center; gap: 8px; }
  .panel { background: var(--panel); border: 1px solid var(--border); border-radius: 12px;
           padding: 20px; margin-top: 12px; }
  .tabs { display: flex; gap: 4px; border-bottom: 1px solid var(--border); margin-bottom: 16px; }
  .tab { padding: 8px 14px; font-size: 13px; color: var(--muted); cursor: pointer;
         border-bottom: 2px solid transparent; }
  .tab.active { color: var(--accent); border-bottom-color: var(--accent); }
  .filtros { display: flex; justify-content: space-between; align-items: center;
             margin-bottom: 14px; gap: 8px; flex-wrap: wrap; }
  .filtros input, .filtros select { background: var(--card); border: 1px solid var(--border);
             color: var(--text); border-radius: 6px; padding: 5px 8px; font-size: 12px; }
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
           gap: 10px; margin-bottom: 14px; }
  .card { background: var(--card); border-radius: 8px; padding: 10px 12px; }
  .card .label { font-size: 11px; color: var(--muted); }
  .card .value { font-size: 18px; font-weight: 500; margin-top: 2px; }
  .chart-box { background: var(--card); border-radius: 8px; padding: 8px; margin-bottom: 14px; }
  table { width: 100%; border-collapse: collapse; font-size: 12px; }
  th, td { padding: 6px 8px; text-align: left; border-top: 1px solid var(--border); }
  th { color: var(--muted); font-weight: 500; border-top: none; }
  .log-row { display: flex; align-items: center; gap: 10px; background: var(--card);
             border-radius: 8px; padding: 8px 10px; margin-bottom: 6px; font-size: 12px; }
  .dot { width: 8px; height: 8px; border-radius: 50%; flex: none; }
  .dot.err { background: var(--danger); } .dot.warn { background: var(--warning); }
  .dot.ok { background: var(--success); } .dot.info { background: var(--muted); }
  .log-meta { color: var(--muted); font-size: 11px; margin-top: 2px; }
  .muted { color: var(--muted); }
  button { background: var(--card); border: 1px solid var(--border); color: var(--text);
           border-radius: 6px; padding: 5px 10px; font-size: 12px; cursor: pointer; }
  button:hover { background: #262b36; }
</style>
</head>
<body>
<div class="wrap">
  <h1>Jacuzzi - Datos almacenados</h1>
  <div class="panel">
    <div class="tabs" id="tabs"></div>
    <div class="filtros">
      <div>
        <input type="date" id="f-desde"> <input type="date" id="f-hasta">
        <button id="btn-filtrar">Filtrar</button>
      </div>
      <button id="btn-exportar">Exportar CSV</button>
    </div>
    <div class="cards" id="cards"></div>
    <div class="chart-box" id="chart-box" style="display:none">
      <svg id="chart-svg" viewBox="0 0 400 90" style="width:100%;height:90px"></svg>
    </div>
    <div id="tabla-o-log"></div>
  </div>
</div>

<script>
// ---------------------------------------------------------------------
// Panel generico: no conoce de antemano los nombres de columna. Todo se
// deduce de /api/schema (que columnas tiene cada tabla) y del contenido
// real de /api/table/{tabla} (que columnas son numericas de verdad, mas
// alla de su tipo declarado en SQLite).
// ---------------------------------------------------------------------

let ESQUEMA = null;
let TABLA_ACTUAL = null;

const COLOR_EVENTO = { err: '#e5534b', warn: '#d9a441', ok: '#3fb950', info: '#8b93a3' };

// Heuristica para decidir el icono/color de una fila de log a partir de
// las columnas que tenga disponibles (reset_reason, event_class...).
// Si la tabla no tiene ninguna de estas columnas, siempre cae en "info".
function claseEvento(fila, columnas) {
  const idx = (nombre) => columnas.indexOf(nombre);
  const iReset = idx('reset_reason');
  const iEvent = idx('event_class');
  if (iReset > -1 && fila[iReset] && fila[iReset] !== 0) return 'err';
  if (iEvent > -1) {
    const v = fila[iEvent];
    if (v === 2) return 'err';
    if (v === 1) return 'warn';
  }
  return 'ok';
}

async function cargarEsquema() {
  const r = await fetch('/api/schema');
  const data = await r.json();
  ESQUEMA = data.tablas;
  const tabsEl = document.getElementById('tabs');
  tabsEl.innerHTML = '';
  Object.keys(ESQUEMA).forEach((tabla, i) => {
    const div = document.createElement('div');
    div.className = 'tab' + (i === 0 ? ' active' : '');
    div.textContent = tabla;
    div.onclick = () => seleccionarTabla(tabla);
    div.dataset.tabla = tabla;
    tabsEl.appendChild(div);
  });
  const primera = Object.keys(ESQUEMA)[0];
  if (primera) seleccionarTabla(primera);
}

function seleccionarTabla(tabla) {
  TABLA_ACTUAL = tabla;
  document.querySelectorAll('.tab').forEach(t => {
    t.classList.toggle('active', t.dataset.tabla === tabla);
  });
  cargarDatos();
}

function fechaFiltro(id) {
  const v = document.getElementById(id).value;
  if (!v) return null;
  return Math.floor(new Date(v + 'T00:00:00').getTime() / 1000);
}

async function cargarDatos() {
  if (!TABLA_ACTUAL) return;
  const desde = fechaFiltro('f-desde');
  const hasta = fechaFiltro('f-hasta');
  let url = `/api/table/${TABLA_ACTUAL}?limite=200`;
  if (desde) url += `&desde=${desde}`;
  if (hasta) url += `&hasta=${hasta}`;
  const r = await fetch(url);
  const data = await r.json();
  render(data.columnas, data.filas);
}

// Detecta que columnas son numericas de verdad mirando los valores reales
// (evita columnas como "flags" o ids que son numeros pero no queremos
// graficar igual que una temperatura; aun asi se muestran en la tabla).
function columnasNumericas(columnas, filas) {
  return columnas.filter((_, i) => {
    if (columnas[i] === 'ts') return false;
    return filas.every(f => typeof f[i] === 'number');
  });
}

function render(columnas, filas) {
  renderTarjetas(columnas, filas);
  renderGrafica(columnas, filas);
  renderTablaOLog(columnas, filas);
}

function renderTarjetas(columnas, filas) {
  const cardsEl = document.getElementById('cards');
  cardsEl.innerHTML = '';
  if (!filas.length) return;
  const ultima = filas[0];
  const numericas = columnasNumericas(columnas, filas).slice(0, 4);
  numericas.forEach(nombre => {
    const i = columnas.indexOf(nombre);
    const div = document.createElement('div');
    div.className = 'card';
    const val = typeof ultima[i] === 'number' ? Math.round(ultima[i] * 10) / 10 : ultima[i];
    div.innerHTML = `<div class="label">${nombre}</div><div class="value">${val}</div>`;
    cardsEl.appendChild(div);
  });
  const cardTotal = document.createElement('div');
  cardTotal.className = 'card';
  cardTotal.innerHTML = `<div class="label">filas mostradas</div><div class="value">${filas.length}</div>`;
  cardsEl.appendChild(cardTotal);
}

function renderGrafica(columnas, filas) {
  const box = document.getElementById('chart-box');
  const svg = document.getElementById('chart-svg');
  const numericas = columnasNumericas(columnas, filas).slice(0, 2);
  if (!numericas.length || filas.length < 2) { box.style.display = 'none'; return; }
  box.style.display = 'block';
  svg.innerHTML = '';
  const colores = ['#d85a30', '#378add'];
  const datos = [...filas].reverse();
  numericas.forEach((nombre, idx) => {
    const i = columnas.indexOf(nombre);
    const valores = datos.map(f => f[i]);
    const min = Math.min(...valores), max = Math.max(...valores) || 1;
    const puntos = valores.map((v, x) => {
      const px = (x / (valores.length - 1)) * 400;
      const py = 85 - ((v - min) / (max - min || 1)) * 80;
      return `${px.toFixed(1)},${py.toFixed(1)}`;
    }).join(' ');
    const linea = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
    linea.setAttribute('points', puntos);
    linea.setAttribute('fill', 'none');
    linea.setAttribute('stroke', colores[idx % colores.length]);
    linea.setAttribute('stroke-width', '2');
    svg.appendChild(linea);
  });
}

function renderTablaOLog(columnas, filas) {
  const cont = document.getElementById('tabla-o-log');
  const numericas = columnasNumericas(columnas, filas);
  // Si la tabla tiene columnas de texto tipicas de eventos (breadcrumb,
  // reset_reason_text...) se muestra como log; si no, como tabla plana.
  const esLog = columnas.some(c => c.includes('breadcrumb') || c.includes('reset_reason'));

  if (esLog) {
    cont.innerHTML = '';
    filas.forEach(fila => {
      const clase = claseEvento(fila, columnas);
      const iTs = columnas.indexOf('ts');
      const fecha = new Date(fila[iTs] * 1000).toLocaleString();
      const resumen = columnas
        .filter(c => c !== 'ts')
        .map((c, idxRel) => `${c}: ${fila[columnas.indexOf(c)]}`)
        .slice(0, 6).join(' · ');
      const row = document.createElement('div');
      row.className = 'log-row';
      row.innerHTML = `<div class="dot ${clase}"></div>
        <div><div>${fecha}</div><div class="log-meta">${resumen}</div></div>`;
      cont.appendChild(row);
    });
    if (!filas.length) cont.innerHTML = '<p class="muted">Sin datos en el rango seleccionado.</p>';
    return;
  }

  // Tabla plana generica: una columna <th> por cada columna real, sin
  // nombres hardcodeados.
  let html = '<table><thead><tr>' + columnas.map(c => `<th>${c}</th>`).join('') + '</tr></thead><tbody>';
  filas.forEach(fila => {
    html += '<tr>' + fila.map((v, i) => {
      if (columnas[i] === 'ts') return `<td>${new Date(v * 1000).toLocaleString()}</td>`;
      return `<td>${v}</td>`;
    }).join('') + '</tr>';
  });
  html += '</tbody></table>';
  if (!filas.length) html = '<p class="muted">Sin datos en el rango seleccionado.</p>';
  cont.innerHTML = html;
}

document.getElementById('btn-filtrar').onclick = cargarDatos;
document.getElementById('btn-exportar').onclick = async () => {
  const r = await fetch(`/api/table/${TABLA_ACTUAL}?limite=2000`);
  const data = await r.json();
  const csv = [data.columnas.join(',')]
    .concat(data.filas.map(f => f.join(',')))
    .join('\\n');
  const blob = new Blob([csv], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `${TABLA_ACTUAL}.csv`;
  a.click();
};

cargarEsquema();
</script>
</body>
</html>
"""


@app.get("/api/health")
def health():
    """Endpoint simple para comprobar que el servidor esta vivo (util
    para monitorizacion externa o para probar la conexion desde el ESP32
    durante el desarrollo)."""
    return {"ok": True, "time": int(time.time())}
