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
<meta name="viewport" content="width=device-width, initial-scale=1.0">
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

.donuts{flex:0 0 auto;display:flex;justify-content:space-between;margin-bottom:8px;}
.donut-box{text-align:center;flex:1;}
.donut-box .val{font-size:11px;color:var(--dim);margin-top:3px;letter-spacing:.2px;}
.donut-box .val b{display:block;font-size:14px;color:#fff;margin-top:0;}

.chart-scroll{flex:1;min-height:0;background:#0d1512;border-radius:8px;position:relative;overflow-x:auto;overflow-y:hidden;-webkit-overflow-scrolling:touch;touch-action:pan-x;}
.chart-scroll::-webkit-scrollbar{display:none;}
.chart-scroll canvas{display:block;height:100%;}
.zoom-hint{position:absolute;bottom:4px;right:8px;font-size:9px;color:var(--dim);background:rgba(0,0,0,.4);padding:2px 6px;border-radius:6px;pointer-events:none;}

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
.evt-card .close-btn{width:100%;margin-top:14px;background:var(--amber);color:#0b1210;border:none;border-radius:8px;padding:10px 0;font-family:var(--mono);font-weight:900;font-size:12px;cursor:pointer;}
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
      <div class="evt-card">
        <div class="handle"></div>
        <div class="title" id="evtTitle">—</div>
        <div class="time" id="evtTime">—</div>
        <div class="rows" id="evtRows"></div>
        <button class="close-btn" onclick="closeEvt()">CERRAR</button>
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
const pageStates = [];  // { zoom, evPts, canvas, scrollEl, samples }

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

function totals(samples){
  const t = {0:0,1:0,2:0};
  for(let i=0;i<samples.length-1;i++) t[samples[i][3]] += (samples[i+1][0]-samples[i][0]);
  return t;
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

  const padL=32, padR=8, padT=8, padB=20;
  const plotW=W-padL-padR, plotH=H-padT-padB;
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
  const hoursStep = zoom>=3 ? 1 : (zoom>=1.8 ? 2 : 3);
  const dayStart = new Date(tMin*1000); dayStart.setHours(0,0,0,0);
  for(let hh=0; hh<=24; hh+=hoursStep){
    const ts = dayStart.getTime()/1000 + hh*3600;
    if(ts < tMin || ts > tMax) continue;
    const x = xOf(ts);
    ctx.beginPath(); ctx.moveTo(x,padT); ctx.lineTo(x,padT+plotH); ctx.stroke();
    ctx.fillText(hh.toString().padStart(2,'0')+':00', x-12, H-6);
  }

  const stripH=6;
  for(let i=0;i<samples.length-1;i++){
    const x1=xOf(samples[i][0]),x2=xOf(samples[i+1][0]);
    ctx.fillStyle=STATE_COLOR[samples[i][3]];
    ctx.fillRect(x1,padT+plotH-stripH,Math.max(x2-x1,1),stripH);
  }

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
    const t = totals(day.samples);
    const totalDay = t[0]+t[1]+t[2] || 1;
    const page = document.createElement('div');
    page.className = 'day-page';
    page.innerHTML =
      '<div class="day-hdr"><span class="name">'+DIAS[day.date.getDay()]+' '+day.date.getDate()+'/'+(day.date.getMonth()+1)+'</span>'+
      (isToday?'<span class="today-badge">HOY</span>':'')+'</div>'+
      '<div class="donuts">'+
        '<div class="donut-box">'+donutSVG(t[2]/totalDay,'#e0672e',54)+'<div class="val">SOLAR<br><b>'+fmtDur(t[2])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[1]/totalDay,'#2fa6c9',54)+'<div class="val">FILTRO<br><b>'+fmtDur(t[1])+'</b></div></div>'+
        '<div class="donut-box">'+donutSVG(t[0]/totalDay,'#9db3a6',54)+'<div class="val">PARADO<br><b>'+fmtDur(t[0])+'</b></div></div>'+
      '</div>'+
      '<div class="chart-scroll"><canvas></canvas><div class="zoom-hint">pellizca o doble-toque: zoom</div></div>';
    pager.appendChild(page);
    pageStates.push({ zoom:1, evPts:[], canvas: page.querySelector('canvas'), scrollEl: page.querySelector('.chart-scroll'), samples: day.samples });

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
}
function redrawAll(){ pageStates.forEach((_,i)=>redrawPage(i)); }

window.addEventListener('resize', redrawAll);

function attachInteractivity(){
  const pager = document.getElementById('pager');
  pager.onscroll = ()=>{
    const idx = Math.round(pager.scrollLeft / pager.clientWidth);
    updateWeekSel(idx);
  };

  pageStates.forEach((st,i)=>{
    let pinchStartDist = null, pinchStartZoom = 1;
    let lastTapTime = 0;

    st.scrollEl.addEventListener('touchstart', e=>{
      if(e.touches.length===2){
        pinchStartDist = Math.hypot(
          e.touches[0].clientX-e.touches[1].clientX,
          e.touches[0].clientY-e.touches[1].clientY
        );
        pinchStartZoom = st.zoom;
      }
    }, {passive:true});

    st.scrollEl.addEventListener('touchmove', e=>{
      if(e.touches.length===2 && pinchStartDist){
        const dist = Math.hypot(
          e.touches[0].clientX-e.touches[1].clientX,
          e.touches[0].clientY-e.touches[1].clientY
        );
        st.zoom = Math.max(1, Math.min(5, pinchStartZoom * (dist/pinchStartDist)));
        redrawPage(i);
      }
    }, {passive:true});

    st.scrollEl.addEventListener('touchend', e=>{
      if(e.touches.length<2) pinchStartDist = null;
    });

    st.canvas.addEventListener('touchend', e=>{
      const now = Date.now();
      if(now - lastTapTime < 300){
        st.zoom = st.zoom>1.5 ? 1 : 2.6;
        redrawPage(i);
        e.preventDefault();
      }
      lastTapTime = now;
    });

    function handleTap(clientX, clientY){
      const rect = st.canvas.getBoundingClientRect();
      const mx = clientX - rect.left, my = clientY - rect.top;
      let best=null, bestDist=20;
      st.evPts.forEach(p=>{
        const d = Math.hypot(p.x-mx, p.y-my);
        if(d<bestDist){ bestDist=d; best=p; }
      });
      if(best) openEvt(best);
    }
    st.canvas.addEventListener('click', e=> handleTap(e.clientX, e.clientY));
  });
}

function openEvt(evPt){
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
  document.getElementById('evtOverlay').classList.add('show');
}
function closeEvt(){ document.getElementById('evtOverlay').classList.remove('show'); }

loadData();
setInterval(loadData, 60000); // refresca cada minuto
</script>
</body>
</html>

)HTMLPAGE";