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
canvas#heapChart{display:block;width:100%;height:120px;}
canvas#resetChart{display:block;width:100%;height:90px;}
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
tr.detalle{display:none;}
tr.detalle td{background:#0d1512;color:var(--dim);font-size:10px;padding:8px 10px;}
tr.detalle .grid{display:flex;flex-wrap:wrap;gap:4px 16px;}
.filtro-row{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--dim);margin-bottom:8px;}
.filtro-row input{accent-color:var(--amber);}
.tabla-scroll{max-height:55vh;overflow-y:auto;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:30px 10px;}
.minigraficas{display:flex;gap:10px;flex-wrap:wrap;}
.minigrafica{flex:1;min-width:220px;}
.minigrafica canvas{height:70px !important;}
.minigrafica .chart-wrap canvas{display:block;width:100%;}
.slider-row{display:flex;align-items:center;gap:12px;}
.slider-row input[type=range]{flex:1;accent-color:var(--amber);}
.slider-val{font-size:13px;color:var(--amber);font-weight:700;min-width:52px;text-align:right;}
.sub-nota{font-size:10px;color:var(--dim);margin-top:6px;}
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

  <div class="panel minigraficas">
    <div class="minigrafica">
      <h2>Stack libre minimo</h2>
      <div class="chart-wrap"><canvas id="stackChart"></canvas></div>
    </div>
    <div class="minigrafica">
      <h2>Loop mas lento por muestra</h2>
      <div class="chart-wrap"><canvas id="loopChart"></canvas></div>
    </div>
    <div class="minigrafica">
      <h2>Señal WiFi (RSSI)</h2>
      <div class="chart-wrap"><canvas id="rssiChart"></canvas></div>
    </div>
    <div class="minigrafica">
      <h2>Errores NTC acumulados</h2>
      <div class="chart-wrap"><canvas id="ntcChart"></canvas></div>
    </div>
  </div>

  <div class="panel" id="panelReinicios" style="display:none;">
    <h2>Uptime alcanzado antes de cada reinicio no normal</h2>
    <div class="chart-wrap"><canvas id="resetChart"></canvas></div>
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
    <label class="filtro-row"><input type="checkbox" id="chkSoloEventos"> Mostrar solo reinicios/eventos</label>
    <div class="tabla-scroll">
      <div id="emptyMsg" style="display:none;">Aun no hay muestras de diagnostico.</div>

      <table id="tablaDiag" style="display:none;">
        <thead>
          <tr><th></th><th>Fecha/hora</th><th>Uptime</th><th>WiFi</th><th>Heap libre</th><th>Motivo arranque</th></tr>
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
// descendente). Reutilizable: recibe los valores ya extraidos, el color
// de trazo y una funcion de formato opcional para las etiquetas de
// referencia (min/max) que se dibujan sobre la propia grafica.
//
// recommendedValue es OPCIONAL: si no se pasa (undefined), la funcion
// se comporta exactamente igual que antes. Si se pasa, se dibuja una
// linea discontinua adicional (distinta de las guias min/max) marcando
// el valor recomendado/seguro para esa metrica, con su propia etiqueta.
function drawLineChart(canvas, values, strokeColorVar, height, fmtFn, recommendedValue){
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth || canvas.parentElement.clientWidth;
  const H = height || 120;
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0,0,W,H);

  if(values.length < 2){
    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--dim');
    ctx.font = '11px monospace';
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
  // padR ampliado para dejar sitio a las etiquetas de valor minimo/maximo
  // (antes no se dibujaba ningun numero de referencia sobre la grafica).
  const padL = 4, padR = 46, padT = 12, padB = 14;
  const plotW = W - padL - padR, plotH = H - padT - padB;

  const x = i => padL + (i/(values.length-1)) * plotW;
  const y = v => padT + plotH - ((v-minV)/range) * plotH;

  const dimColor = getComputedStyle(document.documentElement).getPropertyValue('--dim');
  const strokeColor = getComputedStyle(document.documentElement).getPropertyValue(strokeColorVar);

  // Linea guia horizontal en el minimo y en el maximo, cada una con su
  // valor de referencia escrito al final de la linea.
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

  ctx.fillStyle = dimColor;
  ctx.font = '10px monospace';
  ctx.textBaseline = 'middle';
  ctx.fillText(fmt(maxV), W-padR+4, y(maxV));
  ctx.fillText(fmt(minV), W-padR+4, y(minV));

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

    ctx.fillStyle = 'rgba(255,176,32,0.85)';
    ctx.font = '10px monospace';
    ctx.fillText('rec. ' + fmt(recommendedValue), W-padR+4, y(recommendedValue));
  }

  // Valor de la ultima muestra, en el color de la propia curva, para
  // saber de un vistazo el dato actual sin mirar la tabla.
  ctx.fillStyle = strokeColor;
  ctx.font = 'bold 10px monospace';
  ctx.fillText(fmt(values[values.length-1]), W-padR+4, y(values[values.length-1]) + 12 * Math.sign(y(minV)-y(values[values.length-1])||1));

  ctx.strokeStyle = strokeColor;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  values.forEach((v,i) => { const px=x(i), py=y(v); if(i===0) ctx.moveTo(px,py); else ctx.lineTo(px,py); });
  ctx.stroke();
}

// Dibuja barras con el uptime alcanzado justo antes de cada reinicio "no
// normal" (todo lo que no sea encendido con corriente). Ayuda a ver de un
// vistazo si los reinicios se agrupan (siempre a las pocas horas = algo se
// degrada rapido) o son esporadicos (posible causa externa puntual).
// Cada barra lleva encima su valor (fmtUptime) como referencia, ya que
// antes no se dibujaba ningun numero sobre la grafica.
function drawResetBars(canvas, uptimes){
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth || canvas.parentElement.clientWidth;
  const H = 90;
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0,0,W,H);

  if(uptimes.length === 0){
    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--dim');
    ctx.font = '11px monospace';
    ctx.fillText('Sin reinicios no normales registrados', 10, H/2);
    return;
  }

  const maxV = Math.max(...uptimes) * 1.15 || 1;
  const padL = 4, padR = 4, padT = 14, padB = 6;
  const plotW = W - padL - padR, plotH = H - padT - padB;
  const barGap = 4;
  const barW = Math.max((plotW - barGap*(uptimes.length-1)) / uptimes.length, 2);

  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--red');
  ctx.font = '9px monospace';
  ctx.textAlign = 'center';
  uptimes.forEach((v,i) => {
    // Altura minima visible para que un reinicio con uptime muy bajo
    // (crash a los pocos segundos de arrancar) siga siendo visible como
    // barra, en vez de quedar en 0px e invisible.
    const h = Math.max((v/maxV) * plotH, 2);
    const px = padL + i*(barW+barGap);
    ctx.fillRect(px, padT+plotH-h, barW, h);
    ctx.fillText(fmtUptime(v), px+barW/2, padT+plotH-h-4);
  });
  ctx.textAlign = 'left';
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

  drawLineChart(document.getElementById('heapChart'), samples.map(s=>s[IDX.freeHeap]), '--water', 120, fmtHeap, 40000);
  drawLineChart(document.getElementById('stackChart'), samples.map(s=>s[IDX.stackMin]), '--green', 70, fmtStack, 1000);
  drawLineChart(document.getElementById('loopChart'), samples.map(s=>s[IDX.loopMax]), '--amber', 70, fmtMicros, 100000);
  drawLineChart(document.getElementById('rssiChart'), samples.map(s=>s[IDX.rssi]), '--water', 70, v=>Math.round(v)+' dBm', -70);
  drawLineChart(document.getElementById('ntcChart'), samples.map(s=>s[IDX.ntcErrors]), '--red', 70, v=>Math.round(v));

  // Reinicios no normales: uptime alcanzado justo ANTES de cada uno.
  // OJO: la muestra que trae resetReason es la que se registra nada mas
  // arrancar (diaglogInit), asi que su propio campo "uptime" es ~0 (lleva
  // segundos vivo) y no sirve de nada para ver cuanto aguanto el firmware.
  // Lo que interesa es el uptime de la muestra ANTERIOR (la ultima vez que
  // se supo que seguia vivo antes del cuelgue). Antes se usaba el uptime
  // de la propia muestra de reinicio, por eso la grafica salia siempre
  // vacia/plana (barras a 0). eventClass (0=normal) viene ya calculado
  // desde el ESP32, evitando que una fila corrupta/fantasma cuente como
  // evento (ver el clamp de head/count en diaglogInit).
  const uptimesAntesDeReinicio = [];
  for(let i=0; i<samples.length; i++){
    if(samples[i][IDX.eventClass] > 0 && i>0) uptimesAntesDeReinicio.push(samples[i-1][IDX.uptime]);
  }
  document.getElementById('panelReinicios').style.display = uptimesAntesDeReinicio.length ? 'block' : 'none';
  if(uptimesAntesDeReinicio.length) drawResetBars(document.getElementById('resetChart'), uptimesAntesDeReinicio);

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
      '<td class="toggle">&#9656;</td>'+
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
      tr.querySelector('.toggle').innerHTML = visible ? '&#9656;' : '&#9662;';
    });

    cDiag.appendChild(tr);
    cDiag.appendChild(trDetalle);
  }
}

document.getElementById('chkSoloEventos').addEventListener('change', loadData);


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