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
  --water:#2fa6c9; --water-hot:#e0672e;
  --amber:#e8a33d; --green:#4fd67a; --red:#e2513f; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
html,body{height:100%;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
.wrap{max-width:560px;margin:0 auto;height:calc(100vh - 28px);display:flex;flex-direction:column;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;display:flex;justify-content:space-between;align-items:center;flex:0 0 auto;}
a.back{color:var(--dim);text-decoration:none;font-size:11px;letter-spacing:1px;border:1px solid var(--line);padding:5px 10px;border-radius:6px;}
a.back:hover{color:var(--amber);border-color:var(--amber);}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px;margin-top:0;flex:1;display:flex;flex-direction:column;min-height:0;position:relative;}

.day-pager{display:flex;overflow-x:auto;scroll-snap-type:x mandatory;-webkit-overflow-scrolling:touch;flex:1;min-height:0;border-radius:8px;}
.day-pager::-webkit-scrollbar{display:none;}
.day-page{flex:0 0 100%;scroll-snap-align:start;display:flex;flex-direction:column;min-height:0;padding:0 2px;}

.day-hdr{flex:0 0 auto;display:flex;justify-content:space-between;align-items:baseline;margin-bottom:6px;}
.day-hdr .name{font-size:13px;font-weight:900;color:#fff;letter-spacing:.5px;}
.day-hdr .today-badge{font-size:10px;color:#0b1210;background:var(--amber);padding:3px 8px;border-radius:8px;font-weight:900;}

.donuts{flex:0 0 auto;display:flex;flex-wrap:wrap;justify-content:space-between;row-gap:8px;margin-bottom:8px;}
.donut-box{text-align:center;flex:1 1 18%;min-width:58px;}
.donut-box .val{font-size:10px;color:var(--dim);margin-top:3px;letter-spacing:.2px;}
.donut-box .val b{display:block;font-size:13px;color:#fff;margin-top:0;}
.donut-box .val b.pos{color:var(--green);}
.donut-box .val b.neg{color:var(--red);}

.chart-scroll{flex:1;min-height:0;background:#0d1512;border-radius:8px;position:relative;overflow-x:auto;overflow-y:hidden;-webkit-overflow-scrolling:touch;touch-action:pan-x;}
.chart-scroll::-webkit-scrollbar{display:none;}
.chart-scroll canvas{display:block;height:100%;}
.zoom-hint{position:absolute;bottom:4px;right:8px;font-size:9px;color:var(--dim);background:rgba(0,0,0,.4);padding:2px 6px;border-radius:6px;pointer-events:none;}

.evt-marker{position:absolute;width:22px;height:22px;margin-left:-11px;margin-top:-11px;border-radius:50%;border:3px solid var(--amber);display:none;pointer-events:none;animation:evtPulse 1.4s ease-in-out infinite;}
.evt-marker.show{display:block;}
@keyframes evtPulse{
  0%{ transform:scale(.8); opacity:1; }
  50%{ transform:scale(1.35); opacity:.45; }
  100%{ transform:scale(.8); opacity:1; }
}

.week-strip{flex:0 0 auto;display:flex;gap:4px;margin-top:8px;}
.week-cell{flex:1;height:32px;border-radius:6px;background:#0d1512;border:1px solid var(--line);display:flex;align-items:center;justify-content:center;font-size:10px;color:var(--dim);font-weight:800;cursor:pointer;}
.week-cell.today{border-color:var(--amber);color:var(--amber);}
.week-cell.sel{background:var(--amber);color:#0b1210;border-color:var(--amber);}

.legend{display:flex;gap:14px;font-size:10px;color:var(--dim);margin-top:8px;justify-content:center;flex:0 0 auto;flex-wrap:wrap;}
.legend span{display:inline-flex;align-items:center;gap:4px;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;}
#emptyMsg{color:var(--dim);font-size:12px;text-align:center;padding:40px 10px;}

/* ---- popup de detalle de evento ---- */
.evt-overlay{position:absolute;inset:0;background:rgba(0,0,0,.45);display:none;align-items:flex-end;z-index:10;border-radius:6px;}
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
  <h1>HISTORICO <span style="display:flex;gap:6px;"><a class="back" href="/diag">DIAGNOSTICO</a><a class="back" href="/">&larr; VOLVER</a></span></h1>

  <div class="panel">
    <div class="day-pager" id="pager"></div>
    <div id="emptyMsg" style="display:none;">Aun no hay muestras registradas.</div>
    <div class="week-strip" id="weekStrip"></div>
    <div class="legend">
      <span><i class="dot" style="background:var(--water)"></i>T1 Jacuzzi</span>
      <span><i class="dot" style="background:var(--water-hot)"></i>T2 Solar</span>
      <span><i class="dot" style="background:var(--amber)"></i>Toca un evento para ver detalle</span>
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
const STATE_COLOR = {0:'#3a4a44',1:'#2fa6c9',2:'#e0672e'};
const STATE_LABEL = {0:'PARADO',1:'FILTRANDO',2:'SOLAR (CALENTANDO)'};
const STATE_CLASS = {0:'parado',1:'filtro',2:'solar'};
function stateOf(flags){ if(!(flags & 1)) return 0; return (flags & 4) ? 2 : 1; }

let allSamples = [];   // muestras crudas [ts, t1, t2, flags] tal como llegan de /api/history
let DAYS = [];         // agrupadas por dia local: [{key, date, samples:[ts,t1,t2,state]}]
let TODAY_INDEX = 0;
const pageStates = [];  // { zoom, evPts, canvas, scrollEl, marker, samples }

// Limites de zoom: con el ancho tipico del panel (~530px) y el maximo de
// pixeles por linea que aguantan sin problema todos los navegadores/
// dispositivos con el devicePixelRatio mas alto habitual (3x en moviles),
// ZOOM_MAX*530*3 se queda muy por debajo del limite de canvas de
// cualquier navegador (~16000px), asi que es un rango seguro y de sobra
// para ver el detalle de un dia completo.
const ZOOM_MIN = 1, ZOOM_MAX = 6;

// Evento actualmente abierto en el popup de detalle (indice de dia/pagina
// y de evento dentro de esa pagina), para poder mover el circulo
// amarillo al navegar o al hacer zoom/scroll con el popup abierto.
let curEvtPage = -1, curEvtIndex = -1;

async function loadData(){
  try{
    const res = await fetch('/api/history');
    const data = await res.json();
    allSamples = data.samples || [];
  }catch(e){
    allSamples = [];
  }
  buildDays();
  buildUI();
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

// Bonus termico del dia: grados netos (+/-) ganados o perdidos en T1
// Jacuzzi, tomando la temperatura cada 15 minutos (no las muestras en
// bruto, que llegan mas seguido en cada cambio de estado) desde 1 minuto
// despues del primer arranque de bomba del dia (auto o manual). Cada
// tramo de 15 min solo cuenta si la bomba estuvo en marcha al empezar
// ese tramo; si estaba parada, ese tramo no suma ni resta.
function bonusTermico(samples){
  const firstOnIdx = samples.findIndex(s=>s[3]!==0);
  if(firstOnIdx<0) return 0; // la bomba no arranco ese dia
  const startTs = samples[firstOnIdx][0] + 60; // 1 min despues del primer arranque
  const endTs = samples[samples.length-1][0];
  if(endTs <= startTs) return 0;

  let bonus = 0;
  let prevTs = startTs, prevVal = valueAt(samples, startTs, 1);
  let t = startTs + 900; // rejilla fija cada 15 min
  while(true){
    const point = Math.min(t, endTs);
    const curVal = valueAt(samples, point, 1);
    if(stateAt(samples, prevTs) !== 0) bonus += (curVal - prevVal);
    prevVal = curVal; prevTs = point;
    if(point >= endTs) break;
    t += 900;
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

// Dibuja la grafica de un dia con ancho total = anchoContenedor*zoom (permite scroll horizontal).
// Devuelve los puntos de evento (coordenadas CSS px) para detectar toques sobre ellos.
function drawDayChart(canvas, containerW, containerH, zoom, samples){
  const dpr = window.devicePixelRatio || 1;
  const W = containerW*zoom, H = containerH;
  canvas.style.width = W+'px'; canvas.style.height = H+'px';
  canvas.width = W*dpr; canvas.height = H*dpr;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,W,H);
  if(samples.length < 2) return [];

  // bandH/bandGap: franja de estado (SOLAR/FILTRO/PARADO) separada de la
  // curva, con su propia altura para que se aprecie bien (antes iba
  // pegada dentro de la curva con solo 6px y no se distinguia).
  const padL=32, padR=8, padT=8, padB=20, bandH=14, bandGap=5;
  const plotW=W-padL-padR, plotH=H-padT-padB-bandH-bandGap;
  const tMin=samples[0][0], tMax=samples[samples.length-1][0];
  let vMin=Infinity,vMax=-Infinity;
  samples.forEach(s=>{ vMin=Math.min(vMin,s[1],s[2]); vMax=Math.max(vMax,s[1],s[2]); });
  vMin=Math.floor(vMin-1); vMax=Math.ceil(vMax+1);
  if(vMax-vMin < 4){ vMax+=2; vMin-=2; }
  const xOf=ts=>padL+(ts-tMin)/(tMax-tMin)*plotW;
  const yOf=v=>padT+plotH-(v-vMin)/(vMax-vMin)*plotH;

  ctx.strokeStyle='#1b2622'; ctx.fillStyle='#9db3a6'; ctx.font='9px monospace'; ctx.lineWidth=1;
  for(let i=0;i<=4;i++){
    const v=vMin+(vMax-vMin)*i/4, y=yOf(v);
    ctx.beginPath(); ctx.moveTo(padL,y); ctx.lineTo(W-padR,y); ctx.stroke();
    ctx.fillText(v.toFixed(0)+'°',3,y+3);
  }
  const bandY = padT+plotH+bandGap;
  const hoursStep = zoom>=3 ? 1 : (zoom>=1.8 ? 2 : 3);
  const dayStart = new Date(tMin*1000); dayStart.setHours(0,0,0,0);
  for(let hh=0; hh<=24; hh+=hoursStep){
    const ts = dayStart.getTime()/1000 + hh*3600;
    if(ts < tMin || ts > tMax) continue;
    const x = xOf(ts);
    ctx.strokeStyle='#1b2622';
    ctx.beginPath(); ctx.moveTo(x,padT); ctx.lineTo(x,bandY+bandH); ctx.stroke();
    ctx.fillStyle='#9db3a6';
    ctx.fillText(hh.toString().padStart(2,'0')+':00', x-12, H-6);
  }

  // Franja de estado: separada de la curva y mas alta (bandH) para que
  // se distinga bien de un vistazo, con un borde tenue para marcar sus
  // limites.
  for(let i=0;i<samples.length-1;i++){
    const x1=xOf(samples[i][0]),x2=xOf(samples[i+1][0]);
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
  line(1,'#2fa6c9'); line(2,'#e0672e');

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
  return evPts;
}

function buildUI(){
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
    const bonusColor = res.bonus >= 0 ? '#4fd67a' : '#e2513f';
    const bonusClass = res.bonus > 0 ? 'pos' : (res.bonus < 0 ? 'neg' : '');
    const bonusTxt = (res.bonus >= 0 ? '+' : '') + res.bonus.toFixed(1) + '°';
    const page = document.createElement('div');
    page.className = 'day-page';
    page.innerHTML =
      '<div class="day-hdr"><span class="name">'+DIAS[day.date.getDay()]+' '+day.date.getDate()+'/'+(day.date.getMonth()+1)+'</span>'+
      (isToday?'<span class="today-badge">HOY</span>':'')+'</div>'+
      '<div class="donuts">'+
        '<div class="donut-box">'+donutSVG(t[2]/totalDay,'#e0672e',54)+'<div class="val">SOLAR<br><b>'+fmtDur(t[2])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[1]/totalDay,'#2fa6c9',54)+'<div class="val">FILTRO<br><b>'+fmtDur(t[1])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[0]/totalDay,'#9db3a6',54)+'<div class="val">PARADO<br><b>'+fmtDur(t[0])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(descargasPct,'#e8a33d',54)+'<div class="val">DESCARGAS<br><b>'+res.descargas+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(bonusPct,bonusColor,54)+'<div class="val">BONUS TÉRMICO<br><b class="'+bonusClass+'">'+bonusTxt+'</b></div></div>'+
      '</div>'+
      '<div class="chart-scroll"><canvas></canvas><div class="evt-marker"></div><div class="zoom-hint">pellizca / rueda: zoom · doble-toque: reset</div></div>';
    pager.appendChild(page);
    pageStates.push({ zoom: ZOOM_MIN, evPts:[], canvas: page.querySelector('canvas'), scrollEl: page.querySelector('.chart-scroll'), marker: page.querySelector('.evt-marker'), samples: day.samples });

    const cell = document.createElement('div');
    cell.className = 'week-cell'+(isToday?' today sel':'');
    cell.textContent = DIAS[day.date.getDay()];
    cell.onclick = ()=> goToDay(i);
    weekStrip.appendChild(cell);
  });

  attachInteractivity();
  requestAnimationFrame(()=>{
    redrawAll();
    goToDay(TODAY_INDEX); // al abrir la pagina, se situa en el dia actual
  });
}

function updateWeekSel(idx){
  document.querySelectorAll('.week-cell').forEach((c,i)=>c.classList.toggle('sel', i===idx));
}
function goToDay(idx){
  const pager = document.getElementById('pager');
  if(pager.children[idx]) pager.scrollTo({left: pager.children[idx].offsetLeft, behavior:'smooth'});
}
function redrawPage(i){
  const st = pageStates[i];
  const rect = st.scrollEl.getBoundingClientRect();
  st.evPts = drawDayChart(st.canvas, rect.width, rect.height, st.zoom, st.samples);
  if(i===curEvtPage) positionMarker(i);
}
function redrawAll(){ pageStates.forEach((_,i)=>redrawPage(i)); }

window.addEventListener('resize', redrawAll);

function attachInteractivity(){
  const pager = document.getElementById('pager');
  pager.onscroll = ()=>{
    const idx = Math.round(pager.scrollLeft / pager.clientWidth);
    updateWeekSel(idx);
  };

  function touchDist(t0,t1){
    return Math.hypot(t0.clientX-t1.clientX, t0.clientY-t1.clientY);
  }

  pageStates.forEach((st,i)=>{
    let pinchStartDist = null, pinchStartZoom = 1;
    let rafPending = false;
    let lastTapTime = 0;

    function scheduleRedraw(){
      if(rafPending) return;
      rafPending = true;
      requestAnimationFrame(()=>{ redrawPage(i); rafPending=false; });
    }

    st.scrollEl.addEventListener('touchstart', e=>{
      if(e.touches.length===2){
        pinchStartDist = touchDist(e.touches[0], e.touches[1]);
        pinchStartZoom = st.zoom;
      }
    }, {passive:true});

    // El viewport ya bloquea el zoom nativo del navegador (ver <meta>),
    // asi que aqui es el unico que hace zoom: nada compite contra
    // nosotros y no se dispara sin control. Ademas suavizamos el valor
    // objetivo con un factor de 0.35 en vez de saltar de golpe al valor
    // calculado, para que el pellizco sea fino y progresivo.
    st.scrollEl.addEventListener('touchmove', e=>{
      if(e.touches.length!==2) return;
      if(!pinchStartDist){
        pinchStartDist = touchDist(e.touches[0], e.touches[1]);
        pinchStartZoom = st.zoom;
        return;
      }
      e.preventDefault();
      const dist = touchDist(e.touches[0], e.touches[1]);
      const target = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, pinchStartZoom * (dist / pinchStartDist)));
      st.zoom += (target - st.zoom) * 0.35;
      scheduleRedraw();
    }, {passive:false});

    st.scrollEl.addEventListener('touchend', e=>{
      if(e.touches.length<2) pinchStartDist = null;
    });

    // Zoom con la rueda del raton en PC, centrado en la posicion del cursor
    // no es necesario porque el contenedor ya hace scroll horizontal solo;
    // basta con escalar el zoom de forma progresiva por cada "muesca".
    st.scrollEl.addEventListener('wheel', e=>{
      e.preventDefault();
      const factor = e.deltaY > 0 ? 0.9 : 1.1;
      st.zoom = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, st.zoom * factor));
      redrawPage(i);
    }, {passive:false});

    st.canvas.addEventListener('touchend', e=>{
      const now = Date.now();
      if(now - lastTapTime < 300){
        st.zoom = st.zoom>1.5 ? ZOOM_MIN : Math.min(ZOOM_MAX, 2.6);
        redrawPage(i);
        e.preventDefault();
      }
      lastTapTime = now;
    });

    function handleTap(clientX, clientY){
      const rect = st.canvas.getBoundingClientRect();
      const mx = clientX - rect.left, my = clientY - rect.top;
      let bestIdx = -1, bestDist = 20;
      st.evPts.forEach((p,idx)=>{
        const d = Math.hypot(p.x-mx, p.y-my);
        if(d<bestDist){ bestDist=d; bestIdx=idx; }
      });
      if(bestIdx>=0) openEvt(i, bestIdx);
    }
    st.canvas.addEventListener('click', e=> handleTap(e.clientX, e.clientY));
  });
}

// Mueve el circulo amarillo de "respiracion" sobre el punto del evento
// actualmente abierto en la pagina "pageIdx". Se usa tanto al abrir/
// navegar entre eventos como al redibujar por zoom/scroll/resize.
function positionMarker(pageIdx){
  const st = pageStates[pageIdx];
  const p = st.evPts[curEvtIndex];
  if(!p){ st.marker.classList.remove('show'); return; }
  st.marker.style.left = p.x+'px';
  st.marker.style.top  = p.y+'px';
  st.marker.classList.add('show');
}
function hideAllMarkers(){
  pageStates.forEach(st=> st.marker.classList.remove('show'));
}

// pageIdx: indice del dia (pagina) al que pertenece el evento.
// evtIdx: indice del evento dentro de pageStates[pageIdx].evPts.
function openEvt(pageIdx, evtIdx){
  const st = pageStates[pageIdx];
  const evPt = st.evPts[evtIdx];
  if(!evPt) return;
  hideAllMarkers();
  curEvtPage = pageIdx; curEvtIndex = evtIdx;
  positionMarker(pageIdx);

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
  hideAllMarkers();
  curEvtPage = -1; curEvtIndex = -1;
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

loadData();
setInterval(loadData, 60000); // refresca cada minuto
</script>
</body>
</html>

)HTMLPAGE";