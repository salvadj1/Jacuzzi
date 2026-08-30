/*
 * diagpage.cpp
 * -----------------------------------------------------------------------
 * Contenido HTML/CSS/JS de la pagina "/diag": tabla con el historico de
 * diagnostico (heap libre, WiFi, clientes web, motivo de arranque) para
 * investigar cuelgues sin depender del Monitor Serie. Misma paleta y
 * fuente que el resto de la app (webpage.cpp / datapage.cpp).
 * -----------------------------------------------------------------------
 */
#include "diagpage.h"

const char DIAG_HTML[] PROGMEM = R"HTMLPAGE(

<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Diagnostico - Jacuzzi ESP32</title>
<style>
:root{
  --bg:#0b1210; --panel:#101a17; --line:#22332c; --text:#d8e2dd; --dim:#9db3a6;
  --amber:#e8a33d; --green:#4fd67a; --red:#e2513f; --water:#2fa6c9;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
.wrap{max-width:680px;margin:0 auto;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 10px 4px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;}
.links{display:flex;gap:8px;}
a.back{color:var(--dim);text-decoration:none;font-size:11px;letter-spacing:1px;border:1px solid var(--line);padding:5px 10px;border-radius:6px;}
a.back:hover{color:var(--amber);border-color:var(--amber);}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:12px;margin-bottom:10px;}
.resumen{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:4px;}
.tarjeta{flex:1;min-width:120px;background:#0d1512;border:1px solid var(--line);border-radius:8px;padding:10px;}
.tarjeta .lbl{font-size:10px;color:var(--dim);letter-spacing:.5px;}
.tarjeta .val{font-size:15px;font-weight:900;color:#fff;margin-top:2px;}
.tarjeta .val.ok{color:var(--green);}
.tarjeta .val.warn{color:var(--amber);}
.tarjeta .val.bad{color:var(--red);}
table{width:100%;border-collapse:collapse;font-size:11px;}
th{color:var(--dim);text-align:left;padding:6px 6px;border-bottom:1px solid var(--line);font-weight:700;position:sticky;top:0;background:var(--panel);}
td{padding:6px 6px;border-bottom:1px solid #1b2622;white-space:nowrap;}
tr.evento td{color:var(--amber);font-weight:700;}
.tabla-scroll{max-height:60vh;overflow-y:auto;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:30px 10px;}
</style>
</head>
<body>
<div class="wrap">
  <h1>DIAGNOSTICO
    <span class="links">
      <a class="back" href="/datos">&larr; HISTORICO</a>
      <a class="back" href="/">INICIO</a>
      <a class="back" id="btnBorrar" href="#" style="color:var(--red);border-color:var(--red);">BORRAR REGISTROS</a>
    </span>
  </h1>

  <div class="panel">
    <div class="resumen" id="resumen"></div>
  </div>

  <div class="panel">
    <div class="tabla-scroll">
      <div id="emptyMsg" style="display:none;">Aun no hay muestras de diagnostico.</div>
      <table id="tabla" style="display:none;">
        <thead>
          <tr><th>Fecha/hora</th><th>Uptime</th><th>Heap libre</th><th>Heap min.</th><th>WiFi</th><th>RSSI</th><th>Clientes</th><th>Motivo arranque</th></tr>
        </thead>
        <tbody id="cuerpo"></tbody>
      </table>
    </div>
  </div>
</div>

<script>
// IMPORTANTE: estos indices deben coincidir exactamente con el enum
// esp_reset_reason_t de ESP-IDF (ver esp_system.h). Antes estaba
// desplazado un puesto y mezclaba, por ejemplo, brownout con "salida de
// deep sleep".
const MOTIVOS = {
  0:'Desconocido', 1:'Encendido normal', 2:'Reset externo', 3:'Reinicio por software',
  4:'PANIC (crash)', 5:'Watchdog interno', 6:'Watchdog de tarea',
  7:'Otro watchdog', 8:'Salida deep sleep', 9:'Brownout',
  10:'Reset via SDIO'
};

function fmtUptime(sec){
  const d = Math.floor(sec/86400), h = Math.floor((sec%86400)/3600), m = Math.floor((sec%3600)/60);
  let out='';
  if(d>0) out += d+'d ';
  out += h+'h'+m+'m';
  return out;
}
function fmtFecha(ts){
  if(!ts) return '(sin hora NTP)';
  const d = new Date(ts*1000);
  return d.toLocaleDateString('es-ES')+' '+d.toLocaleTimeString('es-ES');
}
function fmtHeap(bytes){ return (bytes/1024).toFixed(0)+' KB'; }

async function loadData(){
  let data;
  try{
    const res = await fetch('/api/diag');
    data = await res.json();
  }catch(e){
    data = { samples: [] };
  }
  const samples = data.samples || [];
  const tabla = document.getElementById('tabla');
  const cuerpo = document.getElementById('cuerpo');
  const empty = document.getElementById('emptyMsg');

  if(samples.length === 0){
    empty.style.display = 'block';
    tabla.style.display = 'none';
    document.getElementById('resumen').innerHTML = '';
    return;
  }
  empty.style.display = 'none';
  tabla.style.display = 'table';

  // Resumen: ultima muestra + numero de arranques detectados en el historico
  const last = samples[samples.length-1];
  const arranques = samples.filter(s=>s[7] && s[7] !== 1).length; // motivo != 0 y != "encendido normal"
  const heapClass = last[1] < 20000 ? 'bad' : (last[1] < 40000 ? 'warn' : 'ok');
  document.getElementById('resumen').innerHTML =
    '<div class="tarjeta"><div class="lbl">HEAP LIBRE AHORA</div><div class="val '+heapClass+'">'+fmtHeap(last[1])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">HEAP MINIMO HISTORICO</div><div class="val">'+fmtHeap(last[2])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">UPTIME ACTUAL</div><div class="val">'+fmtUptime(last[3])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">REINICIOS NO NORMALES</div><div class="val '+(arranques>0?'warn':'ok')+'">'+arranques+'</div></div>';

  cuerpo.innerHTML = '';
  // Se muestran de mas reciente a mas antigua
  for(let i = samples.length-1; i>=0; i--){
    const s = samples[i];
    const esEvento = s[7] && s[7] !== 1; // motivo de arranque distinto de "encendido normal"
    const tr = document.createElement('tr');
    if(esEvento) tr.className = 'evento';
    tr.innerHTML =
      '<td>'+fmtFecha(s[0])+'</td>'+
      '<td>'+fmtUptime(s[3])+'</td>'+
      '<td>'+fmtHeap(s[1])+'</td>'+
      '<td>'+fmtHeap(s[2])+'</td>'+
      '<td>'+(s[6] ? 'Conectado' : 'Sin red')+'</td>'+
      '<td>'+(s[6] ? s[4]+' dBm' : '-')+'</td>'+
      '<td>'+s[5]+'</td>'+
      '<td>'+(MOTIVOS[s[7]] || (esEvento ? 'Codigo '+s[7] : '-'))+'</td>';
    cuerpo.appendChild(tr);
  }
}

// Borra el historico de diagnostico en el ESP32 (con confirmacion, ya
// que no se puede deshacer) y recarga la tabla al terminar.
document.getElementById('btnBorrar').addEventListener('click', async (ev) => {
  ev.preventDefault();
  if (!confirm('¿Borrar todo el historico de diagnostico? Esta accion no se puede deshacer.')) return;
  try {
    await fetch('/api/diag/clear', { method: 'POST' });
  } catch (e) {
    alert('Error al borrar el historico.');
    return;
  }
  loadData();
});

loadData();
setInterval(loadData, 60000);
</script>
</body>
</html>

)HTMLPAGE";
