/*
 * diagpage.cpp
 * ----------------------------------------------------------------------
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
  --amber:#ffb020; --green:#2eff7a; --red:#ff3b2e; --water:#00c8f0;
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
canvas#heapChart{display:block;width:100%;height:150px;}
.tabs{display:flex;gap:6px;margin-bottom:8px;}
.tab-btn{flex:1;background:#0d1512;border:1px solid var(--line);border-radius:6px;padding:7px 6px;color:var(--dim);font-family:var(--mono);font-size:10px;letter-spacing:.5px;text-transform:uppercase;cursor:pointer;}
.tab-btn.activa{color:var(--amber);border-color:var(--amber);}
table{width:100%;border-collapse:collapse;font-size:11px;}
th{color:var(--dim);text-align:left;padding:6px 6px;border-bottom:1px solid var(--line);font-weight:700;position:sticky;top:0;background:var(--panel);}
td{padding:6px 6px;border-bottom:1px solid #1b2622;white-space:nowrap;}
tr.evento-anomalo td{color:var(--red);font-weight:700;}
tr.evento-deliberado td{color:var(--amber);font-weight:700;}
tr.evento-externo td{color:var(--text);font-weight:700;}
td.toggle{cursor:pointer;color:var(--dim);width:18px;text-align:center;}
.tree-toggle{display:inline-block;width:11px;height:11px;line-height:9px;border:1px solid var(--dim);background:var(--panel);color:var(--dim);font-family:var(--mono);font-size:10px;font-weight:700;text-align:center;cursor:pointer;user-select:none;}
th.tree-toggle{border-color:var(--dim);}
th.tree-toggle:hover, .tree-toggle:hover{border-color:var(--amber);color:var(--amber);}
tr.detalle{display:none;}
tr.detalle td{background:#0d1512;color:var(--dim);font-size:10px;padding:8px 10px;}
tr.detalle .grid{display:flex;flex-wrap:wrap;gap:4px 16px;}
.filtro-row{display:flex;align-items:center;gap:8px;font-size:11px;color:var(--dim);margin-bottom:8px;}
.filtro-row input{accent-color:var(--amber);}
.switch{position:relative;display:inline-block;width:38px;height:20px;flex-shrink:0;}
.switch input{opacity:0;width:0;height:0;position:absolute;}
.switch-slider{position:absolute;inset:0;background:#1b2622;border:1px solid var(--line);border-radius:20px;cursor:pointer;transition:.15s;}
.switch-slider::before{content:'';position:absolute;width:14px;height:14px;left:2px;top:2px;background:var(--dim);border-radius:50%;transition:.15s;}
.switch input:checked + .switch-slider{background:rgba(255,176,32,.25);border-color:var(--amber);}
.switch input:checked + .switch-slider::before{transform:translateX(18px);background:var(--amber);}
.tabla-scroll{max-height:55vh;overflow-y:auto;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:30px 10px;}
canvas#stackChart, canvas#loopChart, canvas#rssiChart, canvas#ntcChart{display:block;width:100%;height:150px;}
.slider-row{display:flex;align-items:center;gap:12px;}
.slider-row input[type=range]{flex:1;accent-color:var(--amber);}
.slider-val{font-size:13px;color:var(--amber);font-weight:700;min-width:52px;text-align:right;}
.sub-nota{font-size:10px;color:var(--dim);margin-top:6px;}
tr.detalle .grid{display:flex;flex-direction:column;gap:4px;}

/* En pantallas de PC el texto se veia demasiado pequeño (en movil es
   correcto por el zoom/densidad del dispositivo): se agranda todo un
   poco a partir de cierto ancho de viewport. */
@media (min-width: 700px){
  body{font-size:22px;}
  h1{font-size:21px;}
  .panel h2{font-size:18px;}
  .tarjeta .lbl{font-size:16px;}
  .tarjeta .val{font-size:27px;}
  .tarjeta .sub{font-size:16px;}
  table{font-size:20px;}
  .tab-btn{font-size:16px;}
  .filtro-row{font-size:20px;}
  .slider-val{font-size:21px;}
  a.back{font-size:18px;}
}
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
    <h2 style="margin-top:12px;">Stack libre minimo</h2>
    <div class="chart-wrap"><canvas id="stackChart"></canvas></div>
    <h2 style="margin-top:12px;">Loop mas lento por muestra</h2>
    <div class="chart-wrap"><canvas id="loopChart"></canvas></div>
    <h2 style="margin-top:12px;">Señal WiFi (RSSI)</h2>
    <div class="chart-wrap"><canvas id="rssiChart"></canvas></div>
    <h2 style="margin-top:12px;">Errores NTC acumulados</h2>
    <div class="chart-wrap"><canvas id="ntcChart"></canvas></div>
  </div>

  <div class="panel">
    <h2>Frecuencia de registro</h2>
    <div class="slider-row">
      <input type="range" id="sliderIntervalo" min="1" max="30" step="1" value="5">
      <span id="lblIntervalo" class="slider-val">5 min</span>
    </div>
    <div class="sub-nota">Mas frecuente = historico mas detallado pero cubre menos tiempo (buffer de tamaño fijo).</div>
  </div>

  <div class="panel">
    <label class="filtro-row">
      <span class="switch"><input type="checkbox" id="chkSoloEventos"><span class="switch-slider"></span></span>
      Mostrar solo reinicios/eventos
    </label>
    <div class="tabla-scroll">
      <div id="emptyMsg" style="display:none;">Aun no hay muestras de diagnostico.</div>

      <table id="tablaDiag" style="display:none;">
        <thead>
          <tr><th><span class="tree-toggle" id="toggleTodo" title="Expandir/contraer todo">+</span></th><th>Fecha/hora</th><th>Uptime</th><th>WiFi</th><th>Heap libre</th><th>Motivo arranque</th></tr>
        </thead>
        <tbody></tbody>
      </table>
    </div>
  </div>
</div>

<script>
// Indices del array de cada muestra devuelto por /api/diag (ver
// diaglogToJson en diaglog.cpp): deben coincidir exactamente. El texto del
// motivo y su severidad ya vienen calculados desde el ESP32 (una sola
// fuente de verdad, ver diaglogResetReasonText/diaglogEventClass), asi que
// aqui no se duplica ninguna tabla de traduccion.
const IDX = {
  ts:0, freeHeap:1, minFreeHeap:2, maxAllocHeap:3, uptime:4,
  loopMax:5, stackMin:6, rssi:7, wsClients:8, wifiOk:9,
  resetReason:10, resetReasonText:11, breadcrumb:12, wifiReconnects:13,
  ntcErrors:14, eventClass:15
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
  const txt = s[IDX.resetReasonText];
  if(!txt) return '-';
  const crumb = s[IDX.breadcrumb];
  return crumb ? (txt+' — en: '+crumb) : txt;
}

// Dibuja una curva generica (heap, stack, loop...) en el tiempo. Sin zoom
// ni ejes interactivos (a diferencia de datapage.cpp): aqui solo interesa
// ver de un vistazo la tendencia (p.ej. una fuga de memoria = pendiente
// descendente). Reutilizable: recibe los valores ya extraidos, sus
// timestamps (para las lineas de hora), el color de trazo y una funcion
// de formato opcional para las etiquetas de referencia (min/max) que se
// dibujan sobre la propia grafica.
//
// timestamps es OPCIONAL (puede venir vacio o con ceros si no hay hora
// NTP): si no hay ningun timestamp valido, simplemente no se dibuja
// ninguna linea de hora.
//
// recommendedValue es OPCIONAL: si no se pasa (undefined), la funcion
// se comporta exactamente igual que antes. Si se pasa, se dibuja una
// linea discontinua adicional (distinta de las guias min/max) marcando
// el valor recomendado/seguro para esa metrica, con su propia etiqueta.
function drawLineChart(canvas, values, timestamps, strokeColorVar, height, fmtFn, recommendedValue){
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth || canvas.parentElement.clientWidth;
  const H = height || 120;
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0,0,W,H);

  // En PC el texto de las graficas tambien se agranda un 50% (igual que
  // el resto de la pagina via la media query >=700px), ya que aqui se
  // dibuja con canvas y no responde al CSS.
  const esPc = window.innerWidth >= 700;
  const fsHora  = esPc ? '14px monospace'      : '9px monospace';
  const fsValor = esPc ? '15px monospace'      : '10px monospace';
  const fsValorNegrita = esPc ? 'bold 15px monospace' : 'bold 10px monospace';

  if(values.length < 2){
    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--dim');
    ctx.font = esPc ? '17px monospace' : '11px monospace';
    ctx.fillText('Datos insuficientes todavia', 10, H/2);
    return;
  }

  const fmt = fmtFn || (v => Math.round(v).toString());
  // Si hay un valor recomendado, se incluye en el rango para que la
  // linea de referencia siempre sea visible aunque quede fuera del
  // rango real de los datos (p.ej. heap actual muy por encima del minimo
  // recomendado).
  const allValues = recommendedValue !== undefined ? [...values, recommendedValue] : values;
  const maxV = Math.max(...allValues) * 1.05;
  const minV = Math.min(...allValues) * 0.95;
  const range = Math.max(maxV - minV, 1);
  const dimColor = getComputedStyle(document.documentElement).getPropertyValue('--dim');
  const strokeColor = getComputedStyle(document.documentElement).getPropertyValue(strokeColorVar);
  const panelColor = getComputedStyle(document.documentElement).getPropertyValue('--panel');

  // Dibuja un texto con un pequeño fondo detras (color del panel), para
  // que se pueda leer aunque quede superpuesto sobre la curva o las
  // lineas de guia. align: 'left'|'right'. baseline siempre 'middle'.
  function labelConFondo(text, px, py, align, color, font){
    ctx.font = font;
    ctx.textAlign = align;
    ctx.textBaseline = 'middle';
    const w = ctx.measureText(text).width;
    const bx = align === 'right' ? px - w - 3 : px - 3;
    ctx.fillStyle = panelColor;
    ctx.fillRect(bx, py-7, w+6, 14);
    ctx.fillStyle = color;
    ctx.fillText(text, px, py);
  }

  // La grafica ocupa TODO el ancho del recuadro: ya no se reserva hueco
  // horizontal para las etiquetas, que ahora se dibujan superpuestas
  // encima de la curva (ver labelConFondo). Solo se deja un margen
  // vertical minimo para la fila de horas debajo del trazado.
  const padL = 4, padR = 4, padT = 12, padB = 16;
  const plotW = W - padL - padR, plotH = H - padT - padB;

  const x = i => padL + (i/(values.length-1)) * plotW;
  const y = v => padT + plotH - ((v-minV)/range) * plotH;

  // Lineas verticales cada media hora en punto (:00 y :30), si hay
  // timestamps reales (requiere hora NTP sincronizada; si no hay ninguno
  // valido, se omite).
  if (timestamps && timestamps.some(t => t > 0)) {
    ctx.strokeStyle = '#1b2622';
    ctx.lineWidth = 1;
    ctx.fillStyle = dimColor;
    ctx.font = fsHora;
    ctx.textAlign = 'center';
    let lastSlotKey = null;
    for (let i = 0; i < timestamps.length; i++) {
      const t = timestamps[i];
      if (!t) continue;
      const d = new Date(t*1000);
      const halfHour = d.getMinutes() < 30 ? 0 : 30;
      const slotKey = d.getFullYear()+'-'+d.getMonth()+'-'+d.getDate()+'-'+d.getHours()+'-'+halfHour;
      if (slotKey === lastSlotKey) continue; // una sola linea por franja de 30min
      lastSlotKey = slotKey;
      const px = x(i);
      ctx.beginPath();
      ctx.moveTo(px, padT);
      ctx.lineTo(px, padT+plotH);
      ctx.stroke();
      ctx.fillText(d.getHours()+':'+(halfHour===0?'00':'30'), px, padT+plotH+11);
    }
    ctx.textAlign = 'left';
  }

  // Linea guia horizontal en el minimo y en el maximo (sin escribir aun
  // su valor: eso se hace despues, superpuesto encima de todo).
  ctx.strokeStyle = '#22332c';
  ctx.lineWidth = 1;
  ctx.setLineDash([2,2]);
  [minV, maxV].forEach(v => {
    ctx.beginPath();
    ctx.moveTo(padL, y(v));
    ctx.lineTo(W-padR, y(v));
    ctx.stroke();
  });
  ctx.setLineDash([]);

  // Linea de referencia del valor recomendado (opcional): trazo
  // discontinuo mas largo que el de min/max para distinguirla a simple
  // vista, en un tono ambar semitransparente (neutro, no choca con el
  // color propio de cada curva).
  if (recommendedValue !== undefined) {
    ctx.strokeStyle = 'rgba(255,176,32,0.55)';
    ctx.lineWidth = 1.2;
    ctx.setLineDash([6,4]);
    ctx.beginPath();
    ctx.moveTo(padL, y(recommendedValue));
    ctx.lineTo(W-padR, y(recommendedValue));
    ctx.stroke();
    ctx.setLineDash([]);
  }

  // La curva en si, a todo lo ancho del recuadro.
  ctx.strokeStyle = strokeColor;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  values.forEach((v,i) => { const px=x(i), py=y(v); if(i===0) ctx.moveTo(px,py); else ctx.lineTo(px,py); });
  ctx.stroke();

  // Etiquetas, dibujadas AL FINAL para que queden por encima de la curva
  // y de las lineas de guia. Posiciones sin cambiar respecto a antes:
  // max/min y valor actual a la derecha (arriba/abajo y centro), valor
  // recomendado a la izquierda, centrado.
  labelConFondo(fmt(maxV), W-padR, y(maxV), 'right', dimColor, fsValor);
  labelConFondo(fmt(minV), W-padR, y(minV), 'right', dimColor, fsValor);
  if (recommendedValue !== undefined) {
    labelConFondo('rec. ' + fmt(recommendedValue), padL, padT+plotH/2, 'left', 'rgba(255,176,32,0.85)', fsValor);
  }
  labelConFondo(fmt(values[values.length-1]), W-padR, padT+plotH/2, 'right', strokeColor, fsValorNegrita);
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
  const tDiag = document.getElementById('tablaDiag');
  const empty = document.getElementById('emptyMsg');

  // Sincroniza el slider con el intervalo real que tiene el ESP32 (puede
  // no coincidir con lo que se ve si se cambio desde otra pestaña/movil).
  if(data.intervalMs && !g_sliderTocado){
    const mins = Math.round(data.intervalMs/60000);
    sliderIntervalo.value = mins;
    lblIntervalo.textContent = mins+' min';
  }

  if(samples.length === 0){
    empty.style.display = 'block';
    tDiag.style.display = 'none';
    document.getElementById('resumen').innerHTML = '';
    return;
  }
  empty.style.display = 'none';
  tDiag.style.display = 'table';

  drawLineChart(document.getElementById('heapChart'), samples.map(s=>s[IDX.freeHeap]), samples.map(s=>s[IDX.ts]), '--water', 150, fmtHeap, 40000);
  drawLineChart(document.getElementById('stackChart'), samples.map(s=>s[IDX.stackMin]), samples.map(s=>s[IDX.ts]), '--green', 150, fmtStack, 1000);
  drawLineChart(document.getElementById('loopChart'), samples.map(s=>s[IDX.loopMax]), samples.map(s=>s[IDX.ts]), '--amber', 150, fmtMicros, 100000);
  drawLineChart(document.getElementById('rssiChart'), samples.map(s=>s[IDX.rssi]), samples.map(s=>s[IDX.ts]), '--water', 150, v=>Math.round(v)+' dBm', -70);
  drawLineChart(document.getElementById('ntcChart'), samples.map(s=>s[IDX.ntcErrors]), samples.map(s=>s[IDX.ts]), '--red', 150, v=>Math.round(v));

  // Resumen: ultima muestra + numero de arranques detectados en el historico
  const last = samples[samples.length-1];
  const arranques = samples.filter(s => s[IDX.eventClass] > 0).length;

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
    '<div class="tarjeta"><div class="lbl">LOOP MAS LENTO</div><div class="val '+loopClass+'">'+fmtMicros(last[IDX.loopMax])+'</div><div class="sub">ultimo periodo de '+Math.round((data.intervalMs||300000)/60000)+' min</div></div>'+
    '<div class="tarjeta"><div class="lbl">UPTIME ACTUAL</div><div class="val">'+fmtUptime(last[IDX.uptime])+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">RECONEXIONES WIFI</div><div class="val '+(last[IDX.wifiReconnects]>3?'warn':'ok')+'">'+last[IDX.wifiReconnects]+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">ERRORES SENSOR NTC</div><div class="val '+(last[IDX.ntcErrors]>0?'warn':'ok')+'">'+last[IDX.ntcErrors]+'</div></div>'+
    '<div class="tarjeta"><div class="lbl">REINICIOS NO NORMALES</div><div class="val '+(arranques>0?'warn':'ok')+'">'+arranques+'</div></div>';

  const cDiag = tDiag.querySelector('tbody');
  cDiag.innerHTML = '';
  document.getElementById('toggleTodo').textContent = '+';

  const soloEventos = document.getElementById('chkSoloEventos').checked;
  const CLASES = {1:'evento-deliberado', 2:'evento-externo', 3:'evento-anomalo'};

  // Se muestran de mas reciente a mas antigua
  for(let i = samples.length-1; i>=0; i--){
    const s = samples[i];
    const ec = s[IDX.eventClass];
    if(soloEventos && ec === 0) continue;

    const tr = document.createElement('tr');
    if(CLASES[ec]) tr.className = CLASES[ec];
    tr.innerHTML =
      '<td class="toggle"><span class="tree-toggle">+</span></td>'+
      '<td>'+fmtFecha(s[IDX.ts])+'</td>'+
      '<td>'+fmtUptime(s[IDX.uptime])+'</td>'+
      '<td>'+(s[IDX.wifiOk] ? s[IDX.rssi]+' dBm' : 'Sin red')+'</td>'+
      '<td>'+fmtHeap(s[IDX.freeHeap])+'</td>'+
      '<td>'+motivoTexto(s)+'</td>';

    const trDetalle = document.createElement('tr');
    trDetalle.className = 'detalle';
    trDetalle.innerHTML = '<td colspan="6"><div class="grid">'+
      '<span>Heap max. asignable: '+fmtHeap(s[IDX.maxAllocHeap])+'</span>'+
      '<span>Stack libre min.: '+fmtStack(s[IDX.stackMin])+'</span>'+
      '<span>Loop mas lento: '+fmtMicros(s[IDX.loopMax])+'</span>'+
      '<span>Clientes web: '+s[IDX.wsClients]+'</span>'+
      '<span>Reconex. WiFi: '+s[IDX.wifiReconnects]+'</span>'+
      '<span>Errores NTC: '+s[IDX.ntcErrors]+'</span>'+
      '</div></td>';

    tr.querySelector('.toggle').addEventListener('click', () => {
      const visible = trDetalle.style.display === 'table-row';
      trDetalle.style.display = visible ? 'none' : 'table-row';
      tr.querySelector('.tree-toggle').textContent = visible ? '+' : '-';
    });

    cDiag.appendChild(tr);
    cDiag.appendChild(trDetalle);
  }
}

document.getElementById('chkSoloEventos').addEventListener('change', loadData);

// Boton de cabecera "expandir/contraer todo": alterna todas las filas de
// detalle a la vez, y deja su propio icono (+/-) reflejando el estado
// resultante para poder volver a alternar.
document.getElementById('toggleTodo').addEventListener('click', () => {
  const btn = document.getElementById('toggleTodo');
  const expandir = btn.textContent === '+';
  document.querySelectorAll('#tablaDiag tr.detalle').forEach(tr => {
    tr.style.display = expandir ? 'table-row' : 'none';
  });
  document.querySelectorAll('#tablaDiag .tree-toggle:not(#toggleTodo)').forEach(icon => {
    icon.textContent = expandir ? '-' : '+';
  });
  btn.textContent = expandir ? '-' : '+';
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

// Slider de frecuencia de registro: se envia al ESP32 al soltar (evento
// "change", no "input") para no saturar de peticiones mientras se arrastra.
const sliderIntervalo = document.getElementById('sliderIntervalo');
const lblIntervalo = document.getElementById('lblIntervalo');
let g_sliderTocado = false;

sliderIntervalo.addEventListener('input', () => {
  g_sliderTocado = true;
  lblIntervalo.textContent = sliderIntervalo.value+' min';
});
sliderIntervalo.addEventListener('change', async () => {
  const ms = sliderIntervalo.value * 60000;
  try{
    await fetch('/api/diag/interval?ms='+ms, { method:'POST' });
  }catch(e){
    alert('No se pudo cambiar el intervalo.');
  }
  g_sliderTocado = false;
});

loadData();
setInterval(loadData, 60000);
</script>
</body>
</html>

)HTMLPAGE";