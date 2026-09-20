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


# ---------------- Panel de administracion (solo lectura, uso propio) ----------------

@app.get("/", response_class=HTMLResponse)
def admin_page():
    """Pagina HTML minima para consultar los datos crudos desde el PC
    (o desde fuera via Tailscale Funnel). No es el panel de control del
    jacuzzi -eso lo sigue sirviendo el ESP32-, es solo para inspeccionar
    lo que hay guardado."""
    with get_db() as conn:
        total = conn.execute("SELECT COUNT(*) FROM samples").fetchone()[0]
        total_diag = conn.execute("SELECT COUNT(*) FROM diag_samples").fetchone()[0]
        ultimas = conn.execute(
            "SELECT ts, t1, t2, flags FROM samples ORDER BY ts DESC LIMIT 50"
        ).fetchall()

    filas = "".join(
        f"<tr><td>{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(r[0]))}</td>"
        f"<td>{r[1]:.1f}</td><td>{r[2]:.1f}</td><td>{r[3]}</td></tr>"
        for r in ultimas
    )
    return f"""
    <html>
    <head>
      <meta charset="utf-8">
      <title>Jacuzzi - Admin datos</title>
      <style>
        body {{ font-family: sans-serif; background:#111; color:#eee; padding:20px; }}
        table {{ border-collapse: collapse; width:100%; max-width:700px; }}
        td, th {{ border:1px solid #333; padding:6px 10px; text-align:left; }}
        th {{ background:#222; }}
      </style>
    </head>
    <body>
      <h1>Jacuzzi - Datos almacenados</h1>
      <p>Total de muestras guardadas: <b>{total}</b></p>
      <p>Total de muestras de diagnostico: <b>{total_diag}</b></p>
      <p>Ultimas 50 muestras:</p>
      <table>
        <tr><th>Fecha</th><th>T1 (jacuzzi)</th><th>T2 (solar)</th><th>flags</th></tr>
        {filas}
      </table>
    </body>
    </html>
    """


@app.get("/api/health")
def health():
    """Endpoint simple para comprobar que el servidor esta vivo (util
    para monitorizacion externa o para probar la conexion desde el ESP32
    durante el desarrollo)."""
    return {"ok": True, "time": int(time.time())}
