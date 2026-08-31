/*
 * diagpage.cpp
 * -----------------------------------------------------------------------
 * Contenido HTML/CSS/JS de la pagina "/diag": tarjetas de estado actual,
 * grafica de heap en el tiempo y tabla con el historico completo (heap,
 * fragmentacion, stack, duracion de loop, wifi, sensores, motivo de
 * arranque y breadcrumb) para investigar cuelgues sin depender del
 * Monitor Serie. Misma paleta y fuente que el resto de la app
 * (webpage.cpp / datapage.cpp).
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
.wrap{max-width:720px;margin:0 auto;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 10px 4px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;}
.links{display:flex;gap:8px;}
a.back{color:var(--dim);text-decoration:none;font-size:11px;letter-spacing:1px;border:1px solid var(--line);padding:5px 10px;border-radius:6px;}
a.back:hover{color:var(--amber);border-color:var(--amber);}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:12px;margin-bottom:10px;}
.panel h2{font-size:10px;letter-spacing:1.5px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 2px;}
.resumen{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:4px;}
.tarjeta{flex:1;min-width:130px;background:#0d1512;border:1px solid var(--line);border-radius:8px;padding:10px;}
.tarjeta .lbl{font-size:10px;color:var(--dim);letter-spacing:.5px;}
.tarjeta .val{font-size:15px;font-weight:900;color:#fff;margin-top:2px;}
.tarjeta .val.ok{color:var(--green);}
.tarjeta .val.warn{color:var(--amber);}
.tarjeta .val.bad{color:var(--red);}
.tarjeta .sub{font-size:9px;color:var(--dim);margin-top:2px;}
.chart-wrap{background:#0d1512;border-radius:8px;padding:6px;}
canvas#heapChart{display:block;width:100%;height:120px;}
.tabs{display:flex;gap:6px;margin-bottom:8px;}
.tab-btn{flex:1;background:#0d1512;border:1px solid var(--line);border-radius:6px;padding:7px 6px;color:var(--dim);font-family:var(--mono);font-size:10px;letter-spacing:.5px;text-transform:uppercase;cursor:pointer;}
.tab-btn.activa{color:var(--amber);border-color:var(--amber);}
table{width:100%;border-collapse:collapse;font-size:11px;}
th{color:var(--dim);text-align:left;padding:6px 6px;border-bottom:1px solid var(--line);font-weight:700;position:sticky;top:0;background:var(--panel);}
td{padding:6px 6px;border-bottom:1px solid #1b2622;white-space:nowrap;}
tr.evento td{color:var(--amber);font-weight:700;}
.tabla-scroll{max-height:55vh;overflow-y:auto;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:30px 10px;}
</style>
</head>
<body>
<div class="wrap">
  <h1>DIAGNOSTICO
    <span class="links">
      <a class="back" id="btnBorrar" href="#" style="color:var(--red);border-color:var(--red);">BORRAR REGISTROS</a>
      <a class="back" href="/">&larr; VOLVER</a>
    </span>
  </h1>

  <div class="panel">
    <div class="resumen" id="resumen"></div>
  </div>

  <div class="panel">
    <h2>Heap libre en el tiempo</h2>
    <div class="chart-wrap"><canvas id="heapChart"></canvas></div>
  </div>

  <div class="panel">
    <div class="tabs">
      <div class="tab-btn activa" data-tab="general">General</div>
      <div class="tab-btn" data-tab="memoria">Memoria / Rendimiento</div>
    </div>
    <div class="tabla-scroll">
      <div id="emptyMsg" style="display:none;">Aun no hay muestras de diagnostico.</div>

      <table id="tablaGeneral" style="display:none;">
        <thead>
          <tr><th>Fecha/hora</th><th>Uptime</th><th>Heap libre</th><th>WiFi</th><th>RSSI</th><th>Clientes</th><th>Motivo arranque</th></tr>
        </thead>
        <tbody></tbody>
      </table>

      <table id="tablaMemoria" style="display:none;">
        <thead>
          <tr><th>Fecha/hora</th><th>Heap libre</th><th>Heap max. asignable</th><th>Stack libre min.</th><th>Loop max.</th><th>Reconex. WiFi</th><th>Errores NTC</th></tr>
        </thead>
        <tbody></tbody>
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

// Indices del array de cada muestra devuelto por /api/diag (ver
// diaglogToJson en diaglog.cpp): deben coincidir exactamente.
const IDX = {
  ts:0, freeHeap:1, minFreeHeap:2, maxAllocHeap:3, uptime:4,
  loopMax:5, stackMin:6, rssi:7, wsClients:8, wifiOk:9,
  resetReason:10, breadcrumb:11, wifiReconnects:12, ntcErrors:13
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
function fmtStack(bytes){ return (bytes/1024).toFixed(1)+' KB'; }
function fmtMicros(us){
  if(us < 1000) return us+' us';
  if(us < 1000000) return (us/1000).toFixed(0)+' ms';
  return (us/1000000).toFixed(1)+' s';
}
function motivoTexto(s){
  const reason = s[IDX.resetReason];
  const esEvento = reason && reason !== 1;
  const base = MOTIVOS[reason] || (esEvento ? 'Codigo '+reason : '-');
  const crumb = s[IDX.breadcrumb];
  return (esEvento && crumb) ? (base+' — en: '+crumb) : base;
}

// Dibuja la curva de heap libre en el tiempo. Sin zoom ni ejes
// interactivos (a diferencia de datapage.cpp): aqui solo interesa ver
// de un vistazo si hay una fuga de memoria (pendiente descendente).
function drawHeapChart(canvas, samples){
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth || canvas.parentElement.clientWidth;
  const H = 120;
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0,0,W,H);

  if(samples.length < 2){
    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--dim');
    ctx.font = '11px monospace';
    ctx.fillText('Datos insuficientes todavia', 10, H/2);
    return;
  }

  const heapVals = samples.map(s => s[IDX.freeHeap]);
  const maxV = Math.max(...heapVals) * 1.05;
  const minV = Math.min(...heapVals) * 0.95;
  const range = Math.max(maxV - minV, 1);
  const padL = 4, padR = 4, padT = 6, padB = 6;
  const plotW = W - padL - padR, plotH = H - padT - padB;

  const x = i => padL + (i/(samples.length-1)) * plotW;
  const y = v => padT + plotH - ((v-minV)/range) * plotH;

  // Linea guia horizontal en el minimo (para ver rapido el peor momento)
  ctx.strokeStyle = '#22332c';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(padL, y(minV));
  ctx.lineTo(W-padR, y(minV));
  ctx.stroke();

  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--water');
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  heapVals.forEach((v,i) => { const px=x(i), py=y(v); if(i===0) ctx.moveTo(px,py); else ctx.lineTo(px,py); });
  ctx.stroke();
}

async function loadData(){
  let data;
  try{
    const res = await fetch('/api/diag');
    data = await res.json();
  }catch(e){
    data = { samples: [] };
  }
  const samples = data.samples || [];
  const tGeneral = document.getElementById('tablaGeneral');
  const tMemoria = document.getElementById('tablaMemoria');
  const empty = document.getElementById('emptyMsg');

  if(samples.length === 0){
    empty.style.display = 'block';
    tGeneral.style.display = 'none';
    tMemoria.style.display = 'none';
    document.getElementById('resumen').innerHTML = '';
    return;
  }
  empty.style.display = 'none';
  aplicarTab(); // decide cual de las 2 tablas mostrar segun la pestaña activa

  drawHeapChart(document.getElementById('heapChart'), samples);

  // Resumen: ultima muestra + numero de arranques detectados en el historico
  const last = samples[samples.length-1];
  const arranques = samples.filter(s => s[IDX.resetReason] && s[IDX.resetReason] !== 1).length;

  const heapClass = last[IDX.freeHeap] < 20000 ? 'bad' : (last[IDX.freeHeap] < 40000 ? 'warn' : 'ok');

  // Fragmentacion: si el mayor bloque asignable de un tiron es mucho
  // menor que el heap libre total, hay hueco fragmentado en trozos
  // pequeños que un malloc grande no puede aprovechar.
  const fragRatio = last[IDX.maxAllocHeap] / Math.max(last[IDX.freeHeap], 1);
  const fragClass = fragRatio < 0.5 ? 'bad' : (fragRatio < 0.75 ? 'warn' : 'ok');

  const stackClass = last[IDX.stackMin] < 1024 ? 'bad' : (last[IDX.stackMin] < 2048 ? 'warn' : 'ok');

  const loopClass = last[IDX.loopMax] > 1000000 ? 'bad' : (last[IDX.loopMax] > 200000 ? 'warn' : 'ok');

  document.getElementById('resumen').innerHTML =
    '<div class="tarjeta"><div class="lbl">HEAP LIBRE AHORA</div><div class="val '+heapClass+'">'+fmtHeap(last[IDX.freeHeap])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">HEAP MAX. ASIGNABLE</div><div class="val '+fragClass+'">'+fmtHeap(last[IDX.maxAllocHeap])+'</div><div class="sub">bloque mas grande de un tiron</div></div>'+
    '<div class="tarjeta"><div class="lbl">HEAP MINIMO HISTORICO</div><div class="val">'+fmtHeap(last[IDX.minFreeHeap])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">STACK LIBRE MINIMO</div><div class="val '+stackClass+'">'+fmtStack(last[IDX.stackMin])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">LOOP MAS LENTO</div><div class="val '+loopClass+'">'+fmtMicros(last[IDX.loopMax])+'</div><div class="sub">ultimo periodo de 5 min</div></div>'+
    '<div class="tarjeta"><div class="lbl">UPTIME ACTUAL</div><div class="val">'+fmtUptime(last[IDX.uptime])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">RECONEXIONES WIFI</div><div class="val '+(last[IDX.wifiReconnects]>3?'warn':'ok')+'">'+last[IDX.wifiReconnects]+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">ERRORES SENSOR NTC</div><div class="val '+(last[IDX.ntcErrors]>0?'warn':'ok')+'">'+last[IDX.ntcErrors]+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">REINICIOS NO NORMALES</div><div class="val '+(arranques>0?'warn':'ok')+'">'+arranques+'</div></div>';

  const cGeneral = tGeneral.querySelector('tbody');
  const cMemoria = tMemoria.querySelector('tbody');
  cGeneral.innerHTML = '';
  cMemoria.innerHTML = '';

  // Se muestran de mas reciente a mas antigua
  for(let i = samples.length-1; i>=0; i--){
    const s = samples[i];
    const esEvento = s[IDX.resetReason] && s[IDX.resetReason] !== 1;

    const trG = document.createElement('tr');
    if(esEvento) trG.className = 'evento';
    trG.innerHTML =
      '<td>'+fmtFecha(s[IDX.ts])+'</td>'+
      '<td>'+fmtUptime(s[IDX.uptime])+'</td>'+
      '<td>'+fmtHeap(s[IDX.freeHeap])+'</td>'+
      '<td>'+(s[IDX.wifiOk] ? 'Conectado' : 'Sin red')+'</td>'+
      '<td>'+(s[IDX.wifiOk] ? s[IDX.rssi]+' dBm' : '-')+'</td>'+
      '<td>'+s[IDX.wsClients]+'</td>'+
      '<td>'+motivoTexto(s)+'</td>';
    cGeneral.appendChild(trG);

    const trM = document.createElement('tr');
    if(esEvento) trM.className = 'evento';
    trM.innerHTML =
      '<td>'+fmtFecha(s[IDX.ts])+'</td>'+
      '<td>'+fmtHeap(s[IDX.freeHeap])+'</td>'+
      '<td>'+fmtHeap(s[IDX.maxAllocHeap])+'</td>'+
      '<td>'+fmtStack(s[IDX.stackMin])+'</td>'+
      '<td>'+fmtMicros(s[IDX.loopMax])+'</td>'+
      '<td>'+s[IDX.wifiReconnects]+'</td>'+
      '<td>'+s[IDX.ntcErrors]+'</td>';
    cMemoria.appendChild(trM);
  }
}

// Pestañas General / Memoria y Rendimiento: solo cambian que tabla se ve,
// los datos ya estan cargados en ambas por loadData().
let tabActiva = 'general';
function aplicarTab(){
  document.getElementById('tablaGeneral').style.display = (tabActiva === 'general') ? 'table' : 'none';
  document.getElementById('tablaMemoria').style.display = (tabActiva === 'memoria') ? 'table' : 'none';
}
document.querySelectorAll('.tab-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('activa'));
    btn.classList.add('activa');
    tabActiva = btn.dataset.tab;
    aplicarTab();
  });
});

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