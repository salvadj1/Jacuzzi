/*
 * datapage.cpp
 * -----------------------------------------------------------------------
 * Contenido HTML/CSS/JS de la pagina "/datos": historico semanal con un
 * dia por pantalla (deslizable), anillos de reparto de tiempo por estado,
 * zoom/desplazamiento dentro de la grafica del dia y detalle de evento al
 * tocar un punto de cambio de estado. Embebida en PROGMEM igual que la
 * pagina principal (webpage.cpp), y con el mismo ancho/paleta de colores.
 * -----------------------------------------------------------------------
 */
#include "datapage.h"

const char DATA_HTML[] PROGMEM = R"HTMLPAGE(

<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Historico - Jacuzzi ESP32</title>
<style>
:root{
  --bg:#0b1210; --panel:#101a17; --line:#22332c; --steel:#3a4a44;
  --water:#00c8f0; --water-hot:#e0672e;
  --amber:#ffb020; --green:#2eff7a; --red:#ff3b2e; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
html,body{height:100%;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
.wrap{max-width:560px;margin:0 auto;height:calc(100vh - 28px);height:calc(100dvh - 28px);display:flex;flex-direction:column;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;display:flex;justify-content:space-between;align-items:center;flex:0 0 auto;}
a.back{color:var(--dim);text-decoration:none;font-size:11px;letter-spacing:1px;border:1px solid var(--line);padding:5px 10px;border-radius:6px;}
a.back:hover{color:var(--amber);border-color:var(--amber);}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px;margin-top:0;flex:1;display:flex;flex-direction:column;min-height:0;position:relative;}

.day-pager{display:flex;overflow-x:auto;scroll-snap-type:x mandatory;-webkit-overflow-scrolling:touch;flex:1;min-height:0;min-width:0;border-radius:8px;}
.day-pager::-webkit-scrollbar{display:none;}
.day-page{flex:0 0 100%;scroll-snap-align:start;display:flex;flex-direction:column;min-height:0;min-width:0;padding:0 2px;}

.day-row{flex:0 0 auto;display:flex;align-items:center;gap:6px;flex-wrap:nowrap;margin-bottom:6px;}
.day-row .name{flex:0 0 auto;font-size:13px;font-weight:900;color:#fff;letter-spacing:.5px;white-space:nowrap;}
.btn-del-dia{flex:0 0 auto;height:30px;font-size:11px;letter-spacing:1px;color:var(--red) !important;background:transparent;border:1px solid var(--red) !important;padding:5px 10px;border-radius:6px;cursor:pointer;font-family:var(--mono);white-space:nowrap;}
.btn-del-dia:hover,.btn-del-dia:active{color:var(--red) !important;border-color:var(--red) !important;}

.donuts{flex:0 0 auto;display:flex;flex-wrap:wrap;justify-content:space-between;row-gap:4px;margin-bottom:4px;}
.donut-box{text-align:center;flex:1 1 18%;min-width:58px;}
.donut-box .val{font-size:10px;color:var(--dim);margin-top:3px;letter-spacing:.2px;}
.donut-box .val b{display:block;font-size:13px;color:#fff;margin-top:0;}
.donut-box .val b.pos{color:var(--green);}
.donut-box .val b.neg{color:var(--red);}

// min-width:0 aqui es la clave del bug del zoom infinito: sin esto,
// el ancho intrinseco del <canvas> (que crece al hacer zoom) empuja
// hacia arriba el ancho de este contenedor y de sus padres (.day-page,
// .day-pager), asi que en el SIGUIENTE redraw "containerW" ya viene
// mas grande de lo real y se multiplica otra vez -> bucle de
// retroalimentacion que revienta el limite de zoom y hace desaparecer
// el canvas. overflow-x:auto NO basta por si solo en todos los
// navegadores para forzarlo a 0.
/* La grafica YA NO usa scroll nativo: el canvas mide siempre lo mismo que su
   contenedor y el zoom/desplazamiento se calculan en JS (drawDayChart y
   attachInteractivity). touch-action:none deja todo el manejo tactil en
   nuestras manos; cursor:grab indica que en PC se puede arrastrar. */
.chart-scroll{flex:1;min-height:0;min-width:0;background:#0d1512;border-radius:8px;position:relative;overflow:hidden;touch-action:none;cursor:grab;user-select:none;-webkit-user-select:none;}
.chart-scroll:active{cursor:grabbing;}
.chart-scroll canvas{display:block;}

.week-strip{flex:1 1 0;min-width:0;display:flex;gap:3px;}
.week-cell{flex:1 1 0;min-width:0;height:30px;border-radius:6px;background:#0d1512;border:1px solid var(--line);display:flex;align-items:center;justify-content:center;font-size:9px;color:var(--dim);font-weight:800;cursor:pointer;padding:0;overflow:hidden;}
.week-cell.today{border-color:var(--amber);color:var(--amber);}
@keyframes blinkDay{0%,100%{background:var(--amber);color:#0b1210;}50%{background:#0d1512;color:var(--amber);}}
.week-cell.sel{background:var(--amber);color:#0b1210;border-color:var(--amber);animation:blinkDay 1.2s ease-in-out infinite;}

/* Siempre UNA sola fila: nowrap + tamaño que se adapta al ancho (10-12 px). */
.legend{display:flex;gap:0 6px;font-size:clamp(10px,3.3vw,12px);color:var(--dim);margin-top:4px;width:100%;justify-content:space-between;flex:0 0 auto;flex-wrap:nowrap;white-space:nowrap;}
.legend span{display:inline-flex;align-items:center;gap:5px;flex:0 0 auto;}
.legend .dot{width:10px;height:10px;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:40px 10px;}

/* ---- popup de detalle de evento ---- */
.evt-overlay{position:absolute;inset:0;background:transparent;display:none;align-items:flex-end;z-index:10;border-radius:6px;}
.evt-overlay.show{display:flex;}
.evt-card{width:100%;background:#101a17;border-top:1px solid var(--line);border-radius:14px 14px 0 0;padding:16px 16px 20px 16px;}
.evt-card .handle{width:34px;height:4px;background:var(--line);border-radius:2px;margin:0 auto 12px auto;}
.evt-card .title{font-size:14px;font-weight:900;margin-bottom:2px;}
.evt-card .title.solar{color:var(--water-hot);}
.evt-card .title.filtro{color:var(--water);}
.evt-card .title.parado{color:var(--dim);}
.evt-card .time{font-size:11px;color:var(--dim);margin-bottom:12px;}
.evt-card .rows{display:flex;flex-direction:column;gap:8px;}
.evt-card .row{display:flex;justify-content:space-between;font-size:12px;border-bottom:1px solid #1b2622;padding-bottom:8px;}
.evt-card .row .lbl{color:#fff;font-weight:800;}
.evt-card .row .val{font-weight:900;}
.evt-card .evt-nav{display:flex;gap:8px;margin-top:14px;}
.evt-card .nav-btn{flex:1;background:#0d1512;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:10px 0;font-family:var(--mono);font-weight:900;font-size:11px;cursor:pointer;}
.evt-card .nav-btn:disabled{opacity:.3;}
</style>
</head>
<body>
<div class="wrap">
  <h1>HISTORICO <a class="back" href="/">&larr; VOLVER</a></h1>

  <div class="panel">
    <div class="day-row">
      <span class="name" id="dayName"></span>
      <div class="week-strip" id="weekStrip"></div>
      <button class="btn-del-dia" id="btnDelDia">BORRAR DIA</button>
    </div>
    <div class="day-pager" id="pager"></div>
    <div id="emptyMsg" style="display:none;">Aun no hay muestras registradas.</div>
    <div class="legend">
      <span><i class="dot" style="background:var(--water)"></i>T1 Jacuzzi</span>
      <span><i class="dot" style="background:var(--water-hot)"></i>T2 Solar</span>
      <span><i class="dot" style="background:rgba(255,176,32,0.6)"></i>Día</span>
      <span><i class="dot" style="background:rgba(0,100,200,0.6)"></i>Noche</span>
    </div>

    <div class="evt-overlay" id="evtOverlay">
      <div class="evt-card" id="evtCard">
        <div class="handle"></div>
        <div class="title" id="evtTitle">—</div>
        <div class="time" id="evtTime">—</div>
        <div class="rows" id="evtRows"></div>
        <div class="evt-nav">
          <button class="nav-btn" id="evtPrev">&lsaquo; ANTERIOR</button>
          <button class="nav-btn" id="evtNext">SIGUIENTE &rsaquo;</button>
        </div>
      </div>
    </div>
  </div>
</div>

<script>
const DIAS = ['DOM','LUN','MAR','MIE','JUE','VIE','SAB'];
// Estado derivado de los flags registrados: 0=parado (bomba off),
// 1=filtrando (bomba on, valvulas en filtro), 2=solar (bomba on, valvulas a solar)
const STATE_COLOR = {0:'#3a4a44',1:'#00c8f0',2:'#e0672e'};
const STATE_LABEL = {0:'PARADO',1:'FILTRANDO',2:'SOLAR (CALENTANDO)'};
const STATE_CLASS = {0:'parado',1:'filtro',2:'solar'};
function stateOf(flags){ if(!(flags & 1)) return 0; return (flags & 4) ? 2 : 1; }

let allSamples = [];   // muestras crudas [ts, t1, t2, flags] tal como llegan de /api/history
let DAYS = [];         // agrupadas por dia local: [{key, date, samples:[ts,t1,t2,state]}]
let TODAY_INDEX = 0;
const pageStates = [];  // { zoom, v0, panTarget, evPts, canvas, scrollEl, samples }

// Zoom de la grafica: 1 = dia completo; con ZOOM_MAX se ve un tramo de
// (dia / ZOOM_MAX). Ya no hay canvas gigante ni scroll nativo: el canvas
// mide siempre lo mismo que su contenedor y solo se dibuja el tramo
// visible (ver drawDayChart), asi que no hay limite de pixeles.
const ZOOM_MIN = 1, ZOOM_MAX = 4;

// Margenes izquierdo/derecho del area de dibujo (px CSS). Los comparten el
// dibujo y los gestos para convertir pixeles <-> tiempo con la misma formula.
const CH_PAD_L = 34, CH_PAD_R = 8;

// Ubicacion (Torrevieja) para calcular amanecer/atardecer. Latitud norte
// positiva; longitud este positiva (oeste negativa).
const SUN_LAT = 37.9787, SUN_LON = -0.6822;

// Fuerza SIEMPRE el zoom a quedar dentro de [ZOOM_MIN, ZOOM_MAX], sin
// importar de donde venga el valor. Si por lo que sea llega NaN o
// Infinity, Math.min/Math.max con NaN devuelven NaN y el zoom quedaria
// roto para siempre - por eso el Number.isFinite es imprescindible.
function clampZoom(z){
  if(!Number.isFinite(z)) return ZOOM_MIN;
  return Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, z));
}

// ---- Vista de la grafica (zoom + desplazamiento) ----
// Cada dia guarda "zoom" (1 = dia completo) y "v0" (instante UNIX, en
// segundos, del borde izquierdo visible). El tramo visible dura
// (duracion del dia / zoom) y siempre queda dentro de las muestras.

// Rango [t0,t1] de las muestras de un dia (t1 > t0 siempre).
function dayRange(st){
  const t0 = st.samples[0][0];
  const t1 = Math.max(st.samples[st.samples.length-1][0], t0+1);
  return [t0, t1];
}

// Deja zoom y v0 dentro de limites validos (v0 NaN/undefined -> inicio).
function clampView(st){
  st.zoom = clampZoom(st.zoom);
  if(!st.samples || st.samples.length < 2){ st.v0 = 0; return; }
  const r = dayRange(st);
  const span = (r[1]-r[0]) / st.zoom;
  const v0 = Number.isFinite(st.v0) ? st.v0 : r[0];
  st.v0 = Math.min(Math.max(v0, r[0]), r[1]-span);
}

// Ancho util (px) del area de dibujo de un canvas de ancho "canvasW".
function plotWidth(canvasW){ return Math.max(1, canvasW - CH_PAD_L - CH_PAD_R); }

// Cambia el zoom manteniendo fijo el instante que hay bajo la posicion "mx"
// (px dentro del canvas): el punto bajo el cursor/dedos no se mueve.
function zoomAt(st, mx, newZoom, canvasW){
  clampView(st);
  const r = dayRange(st), full = r[1]-r[0];
  const frac = Math.min(1, Math.max(0, (mx - CH_PAD_L) / plotWidth(canvasW)));
  const tAt = st.v0 + frac * (full / st.zoom);   // instante bajo "mx"
  st.zoom = clampZoom(newZoom);
  st.v0 = tAt - frac * (full / st.zoom);
  clampView(st);
}

// Desplaza la vista "dxPx" pixeles (positivo = el contenido va hacia la derecha).
function panBy(st, dxPx, canvasW){
  clampView(st);
  const r = dayRange(st);
  const span = (r[1]-r[0]) / st.zoom;
  st.v0 -= dxPx / plotWidth(canvasW) * span;
  clampView(st);
}

// ---- Sol: amanecer / atardecer sin internet ----
// Elevacion del sol (grados sobre el horizonte) en el instante UNIX "tsSec"
// (segundos) para una latitud/longitud dadas. Formulas astronomicas
// simplificadas de NOAA (precision ~1 minuto, de sobra para sombrear la
// grafica). Amanecer/atardecer = elevacion -0.833 (radio solar + refraccion).
function solarElevation(tsSec, latDeg, lonDeg){
  const rad = Math.PI/180;
  const jd = tsSec/86400 + 2440587.5;                 // dia juliano
  const T = (jd - 2451545.0)/36525.0;                 // siglos desde J2000
  const L0 = (280.46646 + T*(36000.76983 + T*0.0003032)) % 360;  // long. media
  const M = 357.52911 + T*(35999.05029 - 0.0001537*T);           // anomalia media
  const ecc = 0.016708634 - T*(0.000042037 + 0.0000001267*T);    // excentricidad
  const Mr = M*rad;
  const C = Math.sin(Mr)*(1.914602 - T*(0.004817 + 0.000014*T))
          + Math.sin(2*Mr)*(0.019993 - 0.000101*T)
          + Math.sin(3*Mr)*0.000289;                             // ecuacion del centro
  const omega = 125.04 - 1934.136*T;
  const lambda = L0 + C - 0.00569 - 0.00478*Math.sin(omega*rad); // long. aparente
  const eps = 23.439291 - 0.0130042*T + 0.00256*Math.cos(omega*rad); // oblicuidad
  const decl = Math.asin(Math.sin(eps*rad)*Math.sin(lambda*rad));    // declinacion
  const yv = Math.tan(eps*rad/2), y2 = yv*yv;
  const L0r = L0*rad;
  const eqTime = 4/rad * (y2*Math.sin(2*L0r) - 2*ecc*Math.sin(Mr)
      + 4*ecc*y2*Math.sin(Mr)*Math.cos(2*L0r) - 0.5*y2*y2*Math.sin(4*L0r)
      - 1.25*ecc*ecc*Math.sin(2*Mr));                            // ecuacion del tiempo (min)
  const minUTC = ((jd + 0.5) % 1) * 1440;                        // minutos desde 00:00 UTC
  const tst = (minUTC + eqTime + 4*lonDeg + 1440) % 1440;        // hora solar verdadera (min)
  const ha = (tst/4 - 180) * rad;                                // angulo horario
  const lat = latDeg*rad;
  return Math.asin(Math.sin(lat)*Math.sin(decl) + Math.cos(lat)*Math.cos(decl)*Math.cos(ha)) / rad;
}

// Color de fondo (RGBA) del cielo segun la elevacion del sol: azul de noche,
// naranja en el amanecer/atardecer (sol en el horizonte) y ambar suave de
// dia. Entre los puntos se interpola: es el degradado del crepusculo.
const SKY_STOPS = [
  { e: -12,    c: [0, 100, 200, 0.14] },    // noche
  { e: -0.833, c: [255, 120, 40, 0.18] },   // amanecer / atardecer
  { e: 6,      c: [255, 176, 32, 0.08] }    // dia
];
function skyColor(elev){
  const S = SKY_STOPS;
  if(elev <= S[0].e) return S[0].c;
  if(elev >= S[S.length-1].e) return S[S.length-1].c;
  for(let k=0; k<S.length-1; k++){
    if(elev <= S[k+1].e){
      const f = (elev - S[k].e) / (S[k+1].e - S[k].e);
      return S[k].c.map((v,j)=> v + (S[k+1].c[j]-v)*f);
    }
  }
  return S[S.length-1].c;
}

// Evento actualmente abierto en el popup de detalle (indice de dia/pagina
// y de evento dentro de esa pagina), para poder mover el circulo
// amarillo al navegar o al hacer zoom/scroll con el popup abierto.
let curEvtPage = -1, curEvtIndex = -1;

async function loadData(){
  // Antes de reconstruir, si la interfaz ya estaba montada (esto es un
  // refresco automatico, no la carga inicial), guardamos el zoom de cada
  // dia y que dia se estaba viendo, para restaurarlo despues de
  // reconstruir. Sin esto, cada refresco automatico (cada 60s) devolvia
  // el zoom a ZOOM_MIN y saltaba al dia de hoy sin avisar, cortando
  // cualquier gesto de zoom en curso.
  const viewByKey = {};
  let visibleDayKey = null;
  if(pageStates.length > 0){
    DAYS.forEach((day,i)=>{ viewByKey[day.key] = { zoom: pageStates[i].zoom, v0: pageStates[i].v0 }; });
    const pager = document.getElementById('pager');
    const idx = Math.round(pager.scrollLeft / (pager.clientWidth || 1));
    if(DAYS[idx]) visibleDayKey = DAYS[idx].key;
  }

  try{
    const res = await fetch('/api/history');
    const data = await res.json();
    allSamples = data.samples || [];
  }catch(e){
    allSamples = [];
  }
  buildDays();
  buildUI(viewByKey, visibleDayKey);
}

function dayKey(d){ return d.getFullYear()+'-'+d.getMonth()+'-'+d.getDate(); }

function buildDays(){
  const byKey = {};
  allSamples.forEach(s=>{
    const d = new Date(s[0]*1000);
    const key = dayKey(d);
    if(!byKey[key]) byKey[key] = { key, date: new Date(d.getFullYear(),d.getMonth(),d.getDate()), samples: [] };
    byKey[key].samples.push([s[0], s[1], s[2], stateOf(s[3])]);
  });
  DAYS = Object.values(byKey).sort((a,b)=> a.date - b.date);
  const todayKey = dayKey(new Date());
  TODAY_INDEX = DAYS.findIndex(d=>d.key===todayKey);
  if(TODAY_INDEX < 0) TODAY_INDEX = DAYS.length - 1;
}

// Interpola linealmente el valor de la columna "idx" (1=T1, 2=T2) de las
// muestras en el instante "ts". Reutilizable para cualquier serie temporal
// con huecos irregulares entre muestras.
function valueAt(samples, ts, idx){
  if(ts <= samples[0][0]) return samples[0][idx];
  const last = samples.length-1;
  if(ts >= samples[last][0]) return samples[last][idx];
  for(let i=0;i<last;i++){
    const t0=samples[i][0], t1=samples[i+1][0];
    if(t0<=ts && t1>=ts){
      if(t1===t0) return samples[i][idx];
      const v0=samples[i][idx], v1=samples[i+1][idx];
      return v0 + (v1-v0)*(ts-t0)/(t1-t0);
    }
  }
  return samples[last][idx];
}

// Estado (0/1/2) vigente en el instante "ts": el de la ultima muestra con
// timestamp <= ts (funcion escalon, el estado no se interpola).
function stateAt(samples, ts){
  let st = samples[0][3];
  for(let i=0;i<samples.length;i++){
    if(samples[i][0] <= ts) st = samples[i][3]; else break;
  }
  return st;
}

// Bonus termico del dia: grados netos (+/-) de T1 Jacuzzi respecto a la
// referencia del dia. La referencia (bonus = 0) es T1 en el instante en
// que TERMINA la primera descarga solar del dia (estado 2 -> otro estado).
// Despues, cada vez que termina una nueva descarga, el bonus se actualiza
// a: T1 al terminar esa descarga - T1 de la referencia (acumulado).
// Una descarga que sigue en curso al final de los datos no cuenta todavia.
// Con 0 o 1 descargas terminadas el bonus es 0.
function bonusTermico(samples){
  let refVal = null;  // T1 al terminar la primera descarga (bonus 0)
  let bonus = 0;
  for(let i=0;i<samples.length-1;i++){
    // Fin de descarga: esta muestra es SOLAR y la siguiente ya no
    if(samples[i][3]===2 && samples[i+1][3]!==2){
      const t1 = samples[i+1][1]; // T1 en el instante en que termina
      if(refVal===null) refVal = t1;   // primera descarga: fija la referencia
      else bonus = t1 - refVal;        // siguientes: actualiza el bonus
    }
  }
  return bonus;
}

// Devuelve { t, descargas, bonus }:
// - t: segundos acumulados en cada estado (0 parado, 1 filtro, 2 solar)
// - descargas: nº de veces que el sistema entra en modo SOLAR en el dia
// - bonus: ver bonusTermico() arriba
function totals(samples){
  const t = {0:0,1:0,2:0};
  let descargas = 0;
  for(let i=0;i<samples.length-1;i++){
    const cur = samples[i], next = samples[i+1];
    t[cur[3]] += (next[0]-cur[0]);
    if(next[3]===2 && cur[3]!==2) descargas++;
  }
  return { t, descargas, bonus: bonusTermico(samples) };
}
function fmtDur(sec){
  const h = Math.floor(sec/3600), m = Math.round((sec%3600)/60);
  return h>0 ? h+'h'+(m?m+'m':'') : m+'m';
}
function donutSVG(pct,color,size){
  size=size||56; const r=size/2-6, c=2*Math.PI*r, cx=size/2, cy=size/2;
  pct = isFinite(pct) ? pct : 0;
  return '<svg width="'+size+'" height="'+size+'" viewBox="0 0 '+size+' '+size+'">'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+r+'" fill="none" stroke="#1b2622" stroke-width="6"/>'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+r+'" fill="none" stroke="'+color+'" stroke-width="6" '+
    'stroke-dasharray="'+c+'" stroke-dashoffset="'+(c*(1-pct))+'" stroke-linecap="round" transform="rotate(-90 '+cx+' '+cy+')"/></svg>';
}

// Dibuja la grafica de un dia. El canvas mide siempre lo mismo que su
// contenedor; solo se dibuja el tramo visible [st.v0, st.v0+dia/zoom].
// "markerIdx" = indice (en la lista de eventos devuelta) del evento
// seleccionado, que se marca con un circulo amarillo dibujado con las
// mismas coordenadas que su punto (o -1 si no hay ninguno).
// Devuelve TODOS los eventos (x,y en px CSS del canvas, aunque esten fuera
// de la vista) para detectar toques y navegar entre ellos.
function drawDayChart(canvas, containerW, containerH, st, markerIdx){
  const samples = st.samples;
  const dpr = window.devicePixelRatio || 1;
  const W = Math.max(1, Math.floor(containerW)), H = containerH;
  canvas.style.width = W+'px'; canvas.style.height = H+'px';
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,W,H);
  if(samples.length < 2) return [];

  // bandH/bandGap: franja de estado (SOLAR/FILTRO/PARADO) separada de la
  // curva, con su propia altura para que se aprecie bien.
  const padL=CH_PAD_L, padR=CH_PAD_R, padT=8, padB=20, bandH=14, bandGap=5;
  const plotW=W-padL-padR, plotH=H-padT-padB-bandH-bandGap;
  clampView(st);
  const rng = dayRange(st);
  const span = (rng[1]-rng[0]) / st.zoom;
  const v0 = st.v0, v1 = v0 + span;              // tramo visible
  // Escala vertical fija para todo el dia (no salta al desplazarse)
  let vMin=Infinity,vMax=-Infinity;
  samples.forEach(s=>{ vMin=Math.min(vMin,s[1],s[2]); vMax=Math.max(vMax,s[1],s[2]); });
  vMin=Math.floor(vMin-1); vMax=Math.ceil(vMax+1);
  if(vMax-vMin < 4){ vMax+=2; vMin-=2; }
  const xOf=ts=>padL+(ts-v0)/span*plotW;
  const yOf=v=>padT+plotH-(v-vMin)/(vMax-vMin)*plotH;

  // Todo lo que sigue se recorta al area de dibujo (al desplazar/zoom
  // parte de las curvas queda fuera de la vista).
  ctx.save();
  ctx.beginPath(); ctx.rect(padL,0,plotW,H); ctx.clip();

  // Fondo del cielo: degradado noche -> amanecer -> dia -> atardecer -> noche
  // segun la elevacion real del sol en Torrevieja (columnas de 3 px).
  const SKY_STEP = 3;
  for(let x=padL; x<padL+plotW; x+=SKY_STEP){
    const w = Math.min(SKY_STEP, padL+plotW-x);
    const ts = v0 + (x + w/2 - padL)/plotW*span;
    const c = skyColor(solarElevation(ts, SUN_LAT, SUN_LON));
    ctx.fillStyle = 'rgba('+Math.round(c[0])+','+Math.round(c[1])+','+Math.round(c[2])+','+c[3].toFixed(3)+')';
    ctx.fillRect(x, padT, w, H-padT-padB);
  }

  ctx.strokeStyle='#1b2622'; ctx.lineWidth=1;
  for(let i=0;i<=4;i++){
    const y=yOf(vMin+(vMax-vMin)*i/4);
    ctx.beginPath(); ctx.moveTo(padL,y); ctx.lineTo(W-padR,y); ctx.stroke();
  }

  const bandY = padT+plotH+bandGap;
  // Marcas de hora cada 2h. Las horas con el sol sobre el horizonte se
  // pintan en blanco; el resto en azul suave.
  const HOURS_STEP = 2;
  const dayStart = new Date(rng[0]*1000); dayStart.setHours(0,0,0,0);
  const hourMarks = [];
  ctx.setLineDash([2,3]); // lineas de hora punteadas
  for(let hh=0; hh<=24; hh+=HOURS_STEP){
    const ts = dayStart.getTime()/1000 + hh*3600;
    if(ts < v0 || ts > v1) continue;
    const x = xOf(ts);
    const isDaylight = solarElevation(ts, SUN_LAT, SUN_LON) > -0.833;
    ctx.strokeStyle = isDaylight ? '#ffffff' : '#7fb8ff';
    ctx.beginPath(); ctx.moveTo(x,padT); ctx.lineTo(x,bandY+bandH); ctx.stroke();
    hourMarks.push({ x:x, hh:hh, isDaylight:isDaylight });
  }
  ctx.setLineDash([]);

  // Franja de estado: separada de la curva y mas alta (bandH), con un
  // borde tenue para marcar sus limites.
  for(let i=0;i<samples.length-1;i++){
    const x1=xOf(samples[i][0]),x2=xOf(samples[i+1][0]);
    if(x2 < padL || x1 > W-padR) continue;
    ctx.fillStyle=STATE_COLOR[samples[i][3]];
    ctx.fillRect(x1,bandY,Math.max(x2-x1,1),bandH);
  }
  ctx.strokeStyle='#0b1210'; ctx.lineWidth=1;
  ctx.strokeRect(padL,bandY,plotW,bandH);

  function line(idx,color){
    ctx.strokeStyle=color; ctx.lineWidth=2.2; ctx.beginPath();
    samples.forEach((s,i)=>{ const x=xOf(s[0]),y=yOf(s[idx]); i===0?ctx.moveTo(x,y):ctx.lineTo(x,y); });
    ctx.stroke();
  }
  line(1,'#00c8f0'); line(2,'#e0672e');

  const evPts = [];
  let prevState=samples[0][3], prevTs=samples[0][0];
  samples.forEach(s=>{
    if(s[3]!==prevState){
      const x=xOf(s[0]), y=yOf(s[1]);
      ctx.fillStyle=STATE_COLOR[s[3]];
      ctx.beginPath(); ctx.arc(x,y,5,0,Math.PI*2); ctx.fill();
      ctx.strokeStyle='#0b1210'; ctx.lineWidth=1.6; ctx.stroke();
      evPts.push({x,y,sample:s, fromState:prevState, sinceTs:prevTs});
      prevState=s[3]; prevTs=s[0];
    }
  });
  ctx.restore();

  // Etiquetas fuera del recorte: grados a la izquierda (12 px monoespaciada)
  // y horas abajo (11 px, fuente fina sans-serif para que se lean bien).
  ctx.fillStyle='#9db3a6'; ctx.font='12px monospace';
  for(let i=0;i<=4;i++){
    const v=vMin+(vMax-vMin)*i/4;
    ctx.fillText(v.toFixed(0)+'°',3,yOf(v)+4);
  }
  ctx.font='300 11px system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif';
  ctx.textAlign='center';
  hourMarks.forEach(m=>{
    ctx.fillStyle = m.isDaylight ? '#ffffff' : '#9db3a6';
    // centrada en la marca, sin salirse del canvas por los extremos
    const lx = Math.min(W-14, Math.max(14, m.x));
    ctx.fillText(m.hh.toString().padStart(2,'0')+':00', lx, H-6);
  });
  ctx.textAlign='left';

  // Circulo amarillo "respirando" sobre el evento seleccionado: se dibuja
  // aqui, con las mismas coordenadas que el punto, asi no puede descolocarse.
  if(markerIdx >= 0 && evPts[markerIdx]){
    const p = evPts[markerIdx];
    if(p.x >= padL && p.x <= W-padR){
      const k = 0.5 - 0.5*Math.cos(2*Math.PI*(performance.now()/1400));
      ctx.globalAlpha = 1 - 0.55*k;
      ctx.strokeStyle = '#ffb020'; ctx.lineWidth = 3;
      ctx.beginPath(); ctx.arc(p.x, p.y, 11*(0.8+0.55*k), 0, Math.PI*2); ctx.stroke();
      ctx.globalAlpha = 1;
    }
  }
  return evPts;
}

function buildUI(viewByKey, visibleDayKey){
  viewByKey = viewByKey || {};
  const pager = document.getElementById('pager');
  const weekStrip = document.getElementById('weekStrip');
  pager.innerHTML = '';
  weekStrip.innerHTML = '';
  pageStates.length = 0;

  if(DAYS.length === 0){
    document.getElementById('emptyMsg').style.display = 'block';
    return;
  }
  document.getElementById('emptyMsg').style.display = 'none';

  DAYS.forEach((day,i)=>{
    const isToday = i===TODAY_INDEX;
    const res = totals(day.samples);
    const t = res.t;
    const totalDay = t[0]+t[1]+t[2] || 1;
    // Anillos de "Descargas" y "Bonus termico" no representan un % de
    // tiempo real como los otros tres, asi que el relleno del anillo es
    // solo orientativo (tope arbitrario para que no se vea siempre vacio
    // ni siempre lleno); el numero de abajo es el dato real.
    const descargasPct = Math.min(res.descargas/6, 1);
    const bonusPct = Math.min(Math.abs(res.bonus)/8, 1);
    const bonusColor = res.bonus >= 0 ? '#2eff7a' : '#ff3b2e';
    const bonusClass = res.bonus > 0 ? 'pos' : (res.bonus < 0 ? 'neg' : '');
    const bonusTxt = (res.bonus >= 0 ? '+' : '') + res.bonus.toFixed(1) + '°';
    const page = document.createElement('div');
    page.className = 'day-page';
    page.innerHTML =
      '<div class="donuts">'+
        '<div class="donut-box">'+donutSVG(t[2]/totalDay,'#e0672e',44)+'<div class="val">SOLAR<br><b>'+fmtDur(t[2])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[1]/totalDay,'#00c8f0',44)+'<div class="val">FILTRO<br><b>'+fmtDur(t[1])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[0]/totalDay,'#9db3a6',44)+'<div class="val">PARADO<br><b>'+fmtDur(t[0])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(descargasPct,'#ffb020',44)+'<div class="val">DESCARGAS<br><b>'+res.descargas+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(bonusPct,bonusColor,44)+'<div class="val">BONUS TÉRMICO<br><b class="'+bonusClass+'">'+bonusTxt+'</b></div></div>'+
      '</div>'+
      '<div class="chart-scroll"><canvas></canvas></div>';
    pager.appendChild(page);
    const savedView = viewByKey[day.key] || {};
    pageStates.push({ zoom: clampZoom(savedView.zoom !== undefined ? savedView.zoom : ZOOM_MIN), v0: savedView.v0, panTarget: null, evPts:[], canvas: page.querySelector('canvas'), scrollEl: page.querySelector('.chart-scroll'), samples: day.samples });

    // La grafica se quedaba mas baja de lo disponible porque el alto
    // del canvas se fijaba una sola vez (redraw inicial via rAF) y no
    // se volvia a ajustar si el layout terminaba de asentarse despues
    // (fuentes, barra de direcciones movil, etc.). Con ResizeObserver
    // se redibuja cada vez que el contenedor cambia de tamano real.
    if(typeof ResizeObserver !== 'undefined'){
      const idx = pageStates.length-1;
      new ResizeObserver(()=>redrawPage(idx)).observe(page.querySelector('.chart-scroll'));
    }

    const cell = document.createElement('div');
    cell.className = 'week-cell'+(isToday?' today sel':'');
    cell.textContent = DIAS[day.date.getDay()];
    cell.onclick = ()=> goToDay(i);
    weekStrip.appendChild(cell);
  });

  attachInteractivity();
  requestAnimationFrame(()=>{
    redrawAll();
    // Si veniamos de un refresco automatico y ese dia sigue existiendo,
    // nos quedamos en el; si no (carga inicial, o el dia ya no esta en
    // DAYS por ejemplo tras borrarlo), vamos al dia de hoy como antes.
    const restoreIdx = visibleDayKey ? DAYS.findIndex(d=>d.key===visibleDayKey) : -1;
    goToDay(restoreIdx >= 0 ? restoreIdx : TODAY_INDEX);
    // Red de seguridad: en algunos navegadores/WebViews el layout final
    // (fuentes, barra de direcciones movil) tarda un poco mas en
    // asentarse que un solo rAF, y el ResizeObserver por si solo no
    // siempre llega a tiempo. Redibujamos otra vez poco despues para
    // que el canvas coja la altura real ya asentada.
    setTimeout(redrawAll, 300);
  });
}

let curDayIdx = 0; // indice del dia mostrado (lo usa el boton BORRAR DIA)
function updateWeekSel(idx){
  curDayIdx = idx;
  document.querySelectorAll('.week-cell').forEach((c,i)=>c.classList.toggle('sel', i===idx));
  const d = DAYS[idx];
  document.getElementById('dayName').textContent = d ? DIAS[d.date.getDay()]+' '+d.date.getDate()+'/'+(d.date.getMonth()+1) : '';
}
function goToDay(idx){
  updateWeekSel(idx);
  const pager = document.getElementById('pager');
  if(pager.children[idx]) pager.scrollTo({left: pager.children[idx].offsetLeft, behavior:'smooth'});
}
// Fuerza a mano la altura de chart-scroll en vez de fiarse solo del
// flex:1 del CSS: el <canvas> es un "elemento reemplazado" y algunos
// motores de renderizado (sobre todo WebViews embebidos) lo miden con
// su tamano intrinseco por defecto (300x150) dentro de un flex,
// dejando huecos igual que en un layout roto aunque min-height:0 este
// puesto en toda la cadena. Calculando aqui la altura disponible a
// partir de elementos SIN canvas (pager, cabecera del dia, donuts) nos
// libramos de esa dependencia por completo.
function computeChartHeight(page, scrollEl){
  const pager = document.getElementById('pager');
  const donuts = page.querySelector('.donuts');
  const donutsStyle = getComputedStyle(donuts);
  const donutsH = donuts.offsetHeight + parseFloat(donutsStyle.marginBottom||0);
  return Math.max(120, pager.clientHeight - donutsH);
}
function redrawPage(i){
  const st = pageStates[i];
  const page = st.scrollEl.closest('.day-page');
  const h = computeChartHeight(page, st.scrollEl);
  st.scrollEl.style.height = h+'px';
  const rect = st.scrollEl.getBoundingClientRect();
  st.evPts = drawDayChart(st.canvas, rect.width, h, st, i===curEvtPage ? curEvtIndex : -1);
}
function redrawAll(){ pageStates.forEach((_,i)=>redrawPage(i)); }

window.addEventListener('resize', redrawAll);

function attachInteractivity(){
  const pager = document.getElementById('pager');
  pager.onscroll = ()=>{
    const idx = Math.round(pager.scrollLeft / pager.clientWidth);
    updateWeekSel(idx);
  };

  // Gestos unificados con Pointer Events (raton y tactil), sin scroll nativo:
  //  - 1 puntero: arrastrar = desplazar; toque corto = seleccionar evento
  //  - 2 punteros: pellizco = zoom centrado entre los dedos (y desplaza)
  //  - rueda: zoom centrado en el cursor; rueda horizontal = desplazar
  //  - doble toque (solo tactil): alterna zoom 1 <-> 2.6
  // Un pellizco NUNCA cuenta como toque ni como doble toque (antes, al
  // soltar los dos dedos saltaban dos touchend seguidos y el codigo de
  // doble toque reseteaba el zoom).
  pageStates.forEach((st,i)=>{
    const el = st.scrollEl;
    const ptrs = new Map();             // pointerId -> {x,y} posicion actual
    let pinchDist = 0, pinchZoom = 1;   // estado inicial del pellizco
    let downX = 0, downY = 0, downT = 0;
    let moved = false, multi = false;   // hubo arrastre / hubo 2 dedos en este gesto
    let lastTapT = 0, lastTapX = 0, lastTapY = 0;
    let rafPending = false;

    function scheduleRedraw(){
      if(rafPending) return;
      rafPending = true;
      requestAnimationFrame(()=>{ rafPending = false; redrawPage(i); });
    }
    function canvasRect(){ return st.canvas.getBoundingClientRect(); }

    el.addEventListener('pointerdown', e=>{
      if(e.pointerType === 'mouse' && e.button !== 0) return;
      st.panTarget = null;                       // el usuario toma el control
      try{ el.setPointerCapture(e.pointerId); }catch(_){}
      ptrs.set(e.pointerId, { x:e.clientX, y:e.clientY });
      if(ptrs.size === 1){
        downX = e.clientX; downY = e.clientY; downT = Date.now();
        moved = false; multi = false;
      } else if(ptrs.size === 2){
        const p = [...ptrs.values()];
        pinchDist = Math.hypot(p[0].x-p[1].x, p[0].y-p[1].y) || 1;
        pinchZoom = st.zoom;
        multi = true;
      }
    });

    el.addEventListener('pointermove', e=>{
      const p = ptrs.get(e.pointerId);
      if(!p) return;
      const rect = canvasRect();
      if(ptrs.size === 1){
        // Umbral de 6 px: por debajo sigue siendo un toque, no un arrastre
        if(!moved && Math.hypot(e.clientX-downX, e.clientY-downY) < 6) return;
        moved = true;
        panBy(st, e.clientX - p.x, rect.width);
        p.x = e.clientX; p.y = e.clientY;
      } else if(ptrs.size === 2){
        const a = [...ptrs.values()];
        const prevMid = (a[0].x + a[1].x) / 2;
        p.x = e.clientX; p.y = e.clientY;
        const b = [...ptrs.values()];
        const dist = Math.hypot(b[0].x-b[1].x, b[0].y-b[1].y) || 1;
        const mid = (b[0].x + b[1].x) / 2;
        moved = true;
        zoomAt(st, mid - rect.left, pinchZoom * dist / pinchDist, rect.width);
        panBy(st, mid - prevMid, rect.width);
      } else {
        return;
      }
      scheduleRedraw();
    });

    // Doble toque (tactil) y seleccion del evento mas cercano al toque.
    function handleTap(e){
      const rect = canvasRect();
      const mx = e.clientX - rect.left, my = e.clientY - rect.top;
      const now = Date.now();
      if(e.pointerType === 'touch' && now - lastTapT < 300 &&
         Math.hypot(e.clientX-lastTapX, e.clientY-lastTapY) < 30){
        lastTapT = 0;
        zoomAt(st, mx, st.zoom > 1.5 ? ZOOM_MIN : 2.6, rect.width);
        scheduleRedraw();
        return;
      }
      lastTapT = now; lastTapX = e.clientX; lastTapY = e.clientY;
      const reach = (e.pointerType === 'touch') ? 28 : 20;
      let best = -1, bestD = reach;
      st.evPts.forEach((p,idx)=>{
        if(p.x < CH_PAD_L || p.x > rect.width - CH_PAD_R) return;  // fuera de la vista
        const d = Math.hypot(p.x-mx, p.y-my);
        if(d < bestD){ bestD = d; best = idx; }
      });
      if(best >= 0) openEvt(i, best);
    }

    function endPointer(e){
      if(!ptrs.has(e.pointerId)) return;
      const wasTap = (e.type === 'pointerup' && ptrs.size === 1 && !multi && !moved &&
                      Date.now() - downT < 500);
      ptrs.delete(e.pointerId);
      if(ptrs.size < 2) pinchDist = 0;
      if(wasTap) handleTap(e);
    }
    el.addEventListener('pointerup', endPointer);
    el.addEventListener('pointercancel', endPointer);

    el.addEventListener('wheel', e=>{
      e.preventDefault();
      st.panTarget = null;
      const rect = canvasRect();
      const dx = e.deltaMode === 1 ? e.deltaX*16 : e.deltaX;   // lineas -> px (Firefox)
      if(Math.abs(e.deltaX) > Math.abs(e.deltaY) * 1.5){
        panBy(st, -dx, rect.width);                           // gesto horizontal: desplazar
      } else {
        zoomAt(st, e.clientX - rect.left, st.zoom * (e.deltaY > 0 ? 0.9 : 1.1), rect.width);
      }
      scheduleRedraw();
    }, {passive:false});
  });
}

// Animacion del circulo amarillo y del centrado suave sobre el evento
// abierto. Mientras el popup de detalle esta abierto (curEvtPage >= 0) se
// redibuja su pagina en cada fotograma; al cerrarlo el bucle se detiene.
let markerRaf = 0;
function markerLoop(){
  markerRaf = 0;
  if(curEvtPage < 0) return;
  const st = pageStates[curEvtPage];
  if(st){
    clampView(st);
    if(st.panTarget !== null){
      const d = st.panTarget - st.v0;
      if(Math.abs(d) < 1){ st.v0 = st.panTarget; st.panTarget = null; }
      else st.v0 += d*0.25;
    }
    redrawPage(curEvtPage);
  }
  markerRaf = requestAnimationFrame(markerLoop);
}
function startMarkerLoop(){
  if(!markerRaf) markerRaf = requestAnimationFrame(markerLoop);
}

// Centra la vista (con animacion suave) en el instante "ts" del evento, para
// que no se pierda de vista si hay zoom aplicado.
function centerViewOn(pageIdx, ts){
  const st = pageStates[pageIdx];
  const r = dayRange(st);
  const span = (r[1]-r[0]) / st.zoom;
  st.panTarget = Math.min(Math.max(ts - span/2, r[0]), r[1]-span);
}

// pageIdx: indice del dia (pagina) al que pertenece el evento.
// evtIdx: indice del evento dentro de pageStates[pageIdx].evPts.
function openEvt(pageIdx, evtIdx){
  const st = pageStates[pageIdx];
  const evPt = st.evPts[evtIdx];
  if(!evPt) return;
  const prevPage = curEvtPage;
  curEvtPage = pageIdx; curEvtIndex = evtIdx;
  if(prevPage >= 0 && prevPage !== pageIdx) redrawPage(prevPage);  // borra el marcador anterior
  centerViewOn(pageIdx, evPt.sample[0]);
  startMarkerLoop();

  const s = evPt.sample;
  const d = new Date(s[0]*1000);
  const hh = d.getHours().toString().padStart(2,'0')+':'+d.getMinutes().toString().padStart(2,'0');
  const sinceD = new Date(evPt.sinceTs*1000);
  const sinceHH = sinceD.getHours().toString().padStart(2,'0')+':'+sinceD.getMinutes().toString().padStart(2,'0');
  const durMin = Math.round((s[0]-evPt.sinceTs)/60);

  const titleEl = document.getElementById('evtTitle');
  titleEl.textContent = 'Cambia a ' + STATE_LABEL[s[3]];
  titleEl.className = 'title '+STATE_CLASS[s[3]];
  document.getElementById('evtTime').textContent = hh + ' h';
  document.getElementById('evtRows').innerHTML =
    '<div class="row"><span class="lbl">Estado anterior</span><span class="val">'+STATE_LABEL[evPt.fromState]+' (desde '+sinceHH+', '+durMin+' min)</span></div>'+
    '<div class="row"><span class="lbl">T1 Jacuzzi</span><span class="val" style="color:var(--water)">'+s[1].toFixed(1)+' °C</span></div>'+
    '<div class="row"><span class="lbl">T2 Solar</span><span class="val" style="color:var(--water-hot)">'+s[2].toFixed(1)+' °C</span></div>';

  document.getElementById('evtPrev').disabled = (curEvtIndex<=0);
  document.getElementById('evtNext').disabled = (curEvtIndex>=st.evPts.length-1);
  document.getElementById('evtOverlay').classList.add('show');
}
function closeEvt(){
  document.getElementById('evtOverlay').classList.remove('show');
  const prevPage = curEvtPage;
  curEvtPage = -1; curEvtIndex = -1;
  if(prevPage >= 0 && pageStates[prevPage]) redrawPage(prevPage);   // quita el marcador
}
function navEvt(dir){
  if(curEvtPage<0) return;
  const target = curEvtIndex + dir;
  if(target<0 || target>=pageStates[curEvtPage].evPts.length) return;
  openEvt(curEvtPage, target);
}

// Cerrar tocando fuera de la tarjeta (en el fondo oscuro), sin boton.
document.getElementById('evtOverlay').addEventListener('click', e=>{
  if(e.target.id === 'evtOverlay') closeEvt();
});
document.getElementById('evtPrev').addEventListener('click', ()=> navEvt(-1));
document.getElementById('evtNext').addEventListener('click', ()=> navEvt(1));

// Deslizar el dedo sobre la tarjeta para pasar al evento anterior/siguiente.
(function(){
  const card = document.getElementById('evtCard');
  let startX = null;
  card.addEventListener('touchstart', e=>{ startX = e.touches[0].clientX; }, {passive:true});
  card.addEventListener('touchend', e=>{
    if(startX===null) return;
    const dx = e.changedTouches[0].clientX - startX;
    startX = null;
    if(Math.abs(dx) < 40) return; // deslizamiento demasiado corto: lo ignoramos
    navEvt(dx<0 ? 1 : -1);
  });
})();

// Borrado del dia que se esta mostrando: pide confirmacion (no se puede
// deshacer) y llama al endpoint con el rango [00:00, 24:00) del dia.
document.getElementById('btnDelDia').addEventListener('click', async () => {
  const day = DAYS[curDayIdx];
  if(!day) return;
  const label = DIAS[day.date.getDay()]+' '+day.date.getDate()+'/'+(day.date.getMonth()+1);
  if (!confirm('¿Borrar todo el registro del '+label+'? Esta accion no se puede deshacer.')) return;
  const fromTs = Math.floor(day.date.getTime()/1000);
  // No sumar 86400 fijo: en el dia de cambio de hora de otono el dia local
  // dura 25h. Se calcula la medianoche local del dia siguiente.
  const nextMidnight = new Date(day.date.getFullYear(), day.date.getMonth(), day.date.getDate()+1);
  const toTs = Math.floor(nextMidnight.getTime()/1000);
  try {
    await fetch('/api/history/deleteday?from='+fromTs+'&to='+toTs, { method: 'POST' });
  } catch (e) {
    alert('Error al borrar el dia.');
    return;
  }
  loadData();
});

loadData();
setInterval(loadData, 60000); // refresca cada minuto
</script>
</body>
</html>

)HTMLPAGE";