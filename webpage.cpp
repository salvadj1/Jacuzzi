/*
 * webpage.cpp
 * -----------------------------------------------------------------------
 * Contenido HTML/CSS/JS completo de la aplicacion web, embebido en la
 * memoria flash del ESP32 (PROGMEM) para no depender de LittleFS.
 * -----------------------------------------------------------------------
 */
#include "webpage.h"

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(

<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Control Jacuzzi ESP32</title>
<style>
:root{
  --bg:#0b1210; --panel:#101a17; --line:#22332c; --steel:#3a4a44;
  --pipe:#2b3b36; --water:#2fa6c9; --water-hot:#e0672e;
  --amber:#e8a33d; --green:#4fd67a; --red:#e2513f; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;}
.wrap{max-width:560px;margin:0 auto;}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:5px;margin-top:5px;}
svg{width:100%;height:auto;display:block;}
.minibox{font-family:var(--mono);display:flex;flex-direction:column;gap:0;background:#0d1512;border:1px solid var(--line);border-radius:6px;padding:5px 5px;}
.cards{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:6px;}
.tcard{background:transparent;border:none;border-bottom:2px solid var(--amber);padding:10px 4px;text-align:left;cursor:default;}
.tcard.clickable{cursor:pointer;}
.tcard-lbl{color:#ffffff;font-size:15px;font-weight:900;letter-spacing:.4px;}
.tcard-val{display:block;color:var(--amber);font-size:30px;font-weight:900;margin-top:2px;}
.minibox .row{display:flex;justify-content:space-between;align-items:center;font-size:14px;color:var(--dim);letter-spacing:.3px;padding:10px 0;border-bottom:1px solid #223229;}
.minibox .row:last-child{border-bottom:none;}
.minibox .row .lbl{color:#ffffff;font-weight:900;font-size:14px;letter-spacing:.4px;}
.minibox .row b{color:#ebed8f;font-weight:900;font-size:15px;}
.minibox .row.mode b{color:var(--green);font-size:16px;}
.discharge-bar{display:none;background:#1a1408;border:1px solid var(--amber);border-radius:6px;padding:8px 12px;margin-bottom:6px;text-align:center;}
.discharge-bar.on{display:block;}
.discharge-bar b{color:var(--amber);font-size:13px;letter-spacing:.5px;}
.minibox .row2{display:grid;grid-template-columns:1fr 1fr;align-items:center;padding:10px 0;border-bottom:1px solid #223229;}
.pillrow{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap;}
.pill{display:flex;align-items:center;gap:6px;font-size:14px;font-weight:700;padding:7px 14px;border-radius:22px;background:#0d2a19;color:#9fe1cb;}
.pill.bad{background:#2a1512;color:#f0997b;}
.pill .pilllbl{opacity:.8;}
.pill b{font-weight:900;}
.valverow{border-top:1px solid #22332c;padding-top:12px;display:flex;flex-direction:column;gap:10px;}
.valveitem{display:flex;justify-content:space-between;align-items:center;gap:8px;}
.vlbl{font-size:13px;color:var(--dim);}
.seg{display:flex;background:#0d1512;border:1px solid var(--line);border-radius:22px;overflow:hidden;}
.seg .opt{padding:6px 14px;font-size:12px;font-weight:700;color:var(--dim);white-space:nowrap;}
#segV1 .opt.active{background:var(--green);color:#04342c;}
#segV2 .opt.active{background:var(--red);color:#fff;}
.clockprogrow{display:flex;gap:12px;}
.clockbox{flex:1;background:#0d1512;border-radius:8px;padding:12px;text-align:center;}
.progbox{flex:1;background:#0d1512;border-radius:8px;padding:12px;}
.cb-lbl{font-size:11px;color:var(--dim);letter-spacing:.5px;display:flex;align-items:center;justify-content:center;gap:3px;}
.progbox .cb-lbl{justify-content:flex-start;}
.cb-time{display:block;font-size:20px;font-weight:900;color:#7fe8ff;margin-top:4px;}
.cb-date{display:block;font-size:11px;color:var(--dim);}
.cb-prog{display:block;font-size:14px;font-weight:900;margin:4px 0;}
.cb-days{display:block;font-size:11px;color:var(--dim);margin-top:3px;}
.nextbanner{display:flex;justify-content:space-between;align-items:center;background:#1a1408;border:1px solid var(--amber);border-radius:8px;padding:9px 12px;margin-top:10px;}
.nb-lbl{font-size:11px;color:var(--amber);letter-spacing:.5px;}
.nb-val{font-size:13px;color:var(--amber);font-weight:900;}
.gear-icon{width:16px;height:16px;}
.minibox .row2:last-child{border-bottom:none;}
.minibox .row2 .cell{display:flex;justify-content:space-between;align-items:center;font-size:14px;color:var(--dim);letter-spacing:.3px;}
.minibox .row2 .cell.right{padding-left:14px;}
.minibox .row2 .cell .lbl{color:#ffffff;font-weight:900;font-size:14px;letter-spacing:.4px;}
.minibox .row2 .cell b{color:#ebed8f;font-weight:900;font-size:15px;}
.minibox .row2 .cell.mode b{color:var(--green);font-size:16px;}
.pipe{fill:none;stroke:var(--pipe);stroke-width:9;stroke-linecap:round;stroke-linejoin:round;}
.pipe-inner{fill:none;stroke:#000;stroke-opacity:0.25;stroke-width:9;stroke-linecap:round;stroke-linejoin:round;}
.flow{fill:none;stroke-width:3.2;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:2 10;opacity:0;transition:opacity .4s;}
.flow.on{opacity:1;animation:dash 1s linear infinite;}
.flow.cold{stroke:var(--water);}
.flow.hot{stroke:var(--water-hot);}
@keyframes dash{to{stroke-dashoffset:-64;}}
.lbl{font-size:11px;fill:#ffffff;letter-spacing:1px;}
.tank-water{transition:fill 10s ease;}
.hot-blink{animation:waterBlink 0.35s step-start infinite;}
@keyframes waterBlink{0%,100%{fill:#ff0000;}50%{fill:#ffffff;}}
.hot-blink-text{animation:textBlink 0.35s step-start infinite;}
@keyframes textBlink{0%,100%{fill:#ff0000;}50%{fill:#ffffff;}}
.val{font-size:12px;fill:var(--text);font-weight:bold;transition:fill 10s ease;}
.valve{cursor:default;}
.valve circle.body{fill:var(--steel);stroke:var(--line);stroke-width:2;}
.valve .stem{stroke:var(--amber);stroke-width:8;stroke-linecap:round;transform-box:fill-box;transform-origin:center;transition:transform 10s ease;}
.valve.closed .stem{transform:rotate(90deg);}
.valve circle.body{display:none;}
.valve .cap{fill:var(--pipe);stroke:none;}
.badge{font-size:10px;letter-spacing:1px;}
.minibox .row.clickable{cursor:pointer;}
.minibox .row.clickable:hover b{color:var(--amber);}
.gear-icon{width:14px;height:14px;vertical-align:middle;margin-left:6px;opacity:.7;}
.tcard.clickable:hover .tcard-lbl{color:var(--amber);}
.temp-ctrl{display:flex;align-items:center;gap:8px;}
.temp-ctrl button{padding:2px 10px;font-size:14px;line-height:1;}
.offset-ctrl{display:none;align-items:center;justify-content:center;gap:16px;font-size:12px;color:var(--dim);margin-top:10px;padding-top:10px;border-top:1px solid var(--line);}
.offset-ctrl.open{display:flex;}
.offset-ctrl button{padding:6px 16px;font-size:16px;line-height:1;}
.offset-ctrl b{color:var(--text);}
.progedit{display:none;flex-direction:column;gap:8px;background:#0a100e;border:1px solid var(--line);border-radius:5px;padding:10px;margin:4px 0 2px 0;font-size:12px;}
.progedit.open{display:flex;}
.progedit .fila{display:flex;align-items:center;gap:8px;flex-wrap:wrap;}
.progedit input[type="time"],.progedit input[type="text"],.progedit input[type="password"]{background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;font-family:var(--mono);padding:3px 6px;}
.progedit .dias{display:flex;gap:4px;}
.progedit .dia{width:22px;height:22px;border:1px solid var(--line);border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:10px;cursor:pointer;color:var(--dim);}
.progedit .dia.on{border-color:var(--green);color:var(--green);}
.progedit .guardar{align-self:flex-end;}
.reset-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.75);z-index:50;align-items:center;justify-content:center;padding:20px;}
.reset-overlay.open{display:flex;}
.reset-box{background:#101a17;border:1px solid var(--line);border-radius:8px;padding:22px;max-width:420px;color:var(--text);font-family:var(--mono);}
.reset-box h3{color:var(--amber);margin-top:0;}
.reset-box p{font-size:13px;line-height:1.5;}
.reset-botones{display:flex;justify-content:flex-end;gap:10px;margin-top:14px;}
.reset-box button.confirmar{background:#4fd67a;color:#0b1210;border:none;}
.reset-ssid{font-size:18px;color:var(--green);font-weight:bold;}
#resetLinkManual{display:inline-block;margin-top:10px;color:var(--amber);}
.led{transition:fill .4s, opacity .4s;}
.led.off{fill:#2a332e;opacity:.6;}
.pump-blade{transform-box:fill-box;transform-origin:center;}
.pump-blade.on{animation:spin 0.6s linear infinite;}
@keyframes spin{to{transform:rotate(360deg);}}
.sun-ray{stroke:#e8a33d;stroke-width:2;opacity:0;animation:ray 2.4s ease-in-out infinite;}
@keyframes ray{0%{opacity:0;}30%{opacity:.55;}100%{opacity:0;}}
.statusbar{display:flex;gap:14px;flex-wrap:wrap;margin-top:8px;padding:10px 14px;background:#0d1512;border:1px solid var(--line);border-radius:6px;font-size:12px;}
.statusbar .item{display:flex;align-items:center;gap:6px;color:var(--dim);}
.statusbar .item b{color:var(--text);}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;}
.dot.g{background:var(--green);box-shadow:0 0 6px var(--green);}
.dot.r{background:var(--red);}
.controls{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;}
.opciones-wrap{position:relative;flex:1 1 auto;display:flex;}
.opciones-wrap>button{width:100%;}
.dropdown{display:none;position:absolute;top:calc(100% + 4px);right:0;flex-direction:column;gap:6px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:6px;min-width:140px;z-index:10;box-shadow:0 4px 12px rgba(0,0,0,.4);}
.dropdown.open{display:flex;}
button{flex:1 1 auto;background:#16211c;color:var(--text);border:1px solid var(--line);border-radius:4px;padding:4px 6px;font-family:var(--mono);font-size:10px;letter-spacing:.3px;line-height:1.3;cursor:pointer;white-space:normal;text-align:center;display:flex;align-items:center;justify-content:center;}
button:hover{border-color:var(--amber);color:var(--amber);}
button.active{border-color:var(--green);color:var(--green);}
button:disabled{opacity:.4;cursor:not-allowed;border-color:var(--line);color:var(--dim);}
</style>
</head>
<body>
<div class="wrap">
<h1>&#9679; ESP32 · CONTROL TÉRMICO JACUZZI</h1>
<div class="controls">
  <button id="btnAuto">MODO<br>AUTO</button>
  <button id="btnPump">BOMBA<br>MANUAL</button>
  <button id="btnForceSolar">SOLAR</button>
  <button id="btnForceBypass" class="active">FILTRACION</button>
  <div class="opciones-wrap">
    <button id="btnOpciones">OPCIONES</button>
    <div id="opcionesRow" class="dropdown">
      <button id="btnWifi">WIFI</button>
      <button onclick="location.href='/datos'">DATOS</button>
      <button onclick="location.href='/diag'">DIAGNOSTICO</button>
      <button id="btnRestart">RESTART</button>
    </div>
  </div>
</div>

<div id="resetOverlay" class="reset-overlay">
  <div class="reset-box">
    <h3>Configuración WiFi activada</h3>
    <p>El dispositivo ha abierto una red de configuración. Ve a los
    ajustes WiFi de tu móvil/PC y conéctate a:</p>
    <p class="reset-ssid">Jacuzzi-Config</p>
    <p>Contraseña: <b>12345678</b></p>
    <p>Al conectarte, la página de configuración se abrirá sola. Si no,
    entra a <a id="resetLinkManual" href="http://192.168.4.1/" target="_blank">192.168.4.1</a>.</p>
    <div class="reset-botones">
      <button id="btnResetCancelar">ENTENDIDO</button>
    </div>
  </div>
</div>
<div class="panel">
<svg viewBox="0 0 560 885" xmlns="http://www.w3.org/2000/svg">

  <!-- ===== RECUADRO DE ESTADO (fuera del grupo desplazado, arriba del todo) ===== -->
  <foreignObject x="10" y="0" width="540" height="560">
    <div xmlns="http://www.w3.org/1999/xhtml" class="minibox">

      <div class="discharge-bar" id="dischargeBar">
        <b>DESCARGA SOLAR EN CURSO · <span id="dischargeTime">—</span></b>
      </div>

      <div class="cards">
        <div class="tcard clickable" id="rowT1">
          <div class="tcard-lbl">T1 JACUZZI</div>
          <b class="tcard-val" id="statT1">—</b>
          <div class="offset-ctrl" id="offsetT1Ctrl">
            OFFSET<button id="offT1Down">−</button><b id="offT1Val">—</b><button id="offT1Up">+</button>
          </div>
        </div>
        <div class="tcard clickable" id="rowT2">
          <div class="tcard-lbl">T2 SOLAR</div>
          <b class="tcard-val" id="statT2">—</b>
          <div class="offset-ctrl" id="offsetT2Ctrl">
            OFFSET<button id="offT2Down">−</button><b id="offT2Val">—</b><button id="offT2Up">+</button>
          </div>
        </div>
      </div>

      <div class="cards">
        <div class="tcard clickable" id="rowTempObj">
          <div class="tcard-lbl">TEMP. OBJETIVO</div>
          <b class="tcard-val" id="tempSetVal">—</b>
          <div class="offset-ctrl" id="tempObjCtrl">
            <button id="tempDown">−</button><button id="tempUp">+</button>
          </div>
        </div>
        <div class="tcard clickable" id="rowTempDis">
          <div class="tcard-lbl">TEMP. DESCARGA SOLAR</div>
          <b class="tcard-val" id="solarDisVal">—</b>
          <div class="offset-ctrl" id="tempDisCtrl">
            <button id="solarDisDown">−</button><button id="solarDisUp">+</button>
          </div>
        </div>
      </div>
    </div>

    <div xmlns="http://www.w3.org/1999/xhtml" class="minibox" style="margin-top:8px;">
      <div class="pillrow">
        <span class="pill" id="pillBomba"><span class="pilllbl">BOMBA</span><b id="statPump">—</b></span>
        <span class="pill" id="pillModo"><span class="pilllbl">MODO</span><b id="modeText">—</b></span>
      </div>
      <div class="valverow">
        <div class="valveitem">
          <span class="vlbl">V1 (RUTA AGUA)</span>
          <span class="seg" id="segV1"><span class="opt" data-v="filtro">FILTRO</span><span class="opt" data-v="serpentin">SERPENTÍN</span></span>
        </div>
        <div class="valveitem">
          <span class="vlbl">V2 (RETORNO)</span>
          <span class="seg" id="segV2"><span class="opt" data-v="cerrada">CERRADA</span><span class="opt" data-v="abierta">ABIERTA</span></span>
        </div>
      </div>
    </div>

    <div xmlns="http://www.w3.org/1999/xhtml" class="minibox" style="margin-top:8px;">
      <div class="clockprogrow">
        <div class="clockbox clickable" id="clockRow">
          <div class="cb-lbl">HORA ACTUAL
            <svg class="gear-icon" viewBox="0 0 24 24" fill="none" stroke="#7fe8ff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
          </div>
          <b class="cb-time" id="statClock">—</b>
          <span class="cb-date" id="statClockDate">—</span>
        </div>
        <div class="progbox clickable" id="progRow">
          <div class="cb-lbl">PROGRAMA
            <svg class="gear-icon" viewBox="0 0 24 24" fill="none" stroke="#7fe8ff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
          </div>
          <b class="cb-prog" id="progText">—</b>
          <span class="cb-days" id="progDaysTxt">—</span>
        </div>
      </div>
      <div class="nextbanner" id="nextBanner">
        <span class="nb-lbl">SIGUIENTE PROGRAMA</span>
        <b class="nb-val" id="nextProgText">—</b>
      </div>
      <div class="progedit" id="clockEdit">
        <div class="fila">
          Fecha <input type="date" id="clockSetDate">
          Hora <input type="time" id="clockSet" step="1">
        </div>
        <button class="guardar" id="clockGuardar">GUARDAR</button>
      </div>

      <div class="progedit" id="progEdit">
        <div class="fila">
          Inicio <input type="time" id="progStart">
          Fin <input type="time" id="progEnd">
        </div>
        <div class="fila dias" id="progDias"></div>
        <button class="guardar" id="progGuardar">GUARDAR</button>
      </div>
    </div>
  </foreignObject>

<g transform="translate(-20,165) scale(1.08)">

  <!-- ===== SERPENTIN SOLAR: S apretadas (media S extra al final), bajado 100px; separado 10px extra del filtro alargando las tuberias que suben/bajan ===== -->
  <path class="pipe" d="M 320,570 L 320,456 L 320,416 A 10,10 0 0 1 341.33,416 L 341.33,456 A 10,10 0 0 0 362.67,456 L 362.67,416 A 10,10 0 0 1 384,416 L 384,456 A 10,10 0 0 0 405.33,456 L 405.33,416 A 10,10 0 0 1 426.67,416 L 426.67,456 A 10,10 0 0 0 448,456 L 448,416 A 10,10 0 0 1 469.33,416 L 469.33,456 A 10,10 0 0 0 490.67,456 L 490.67,416 A 10,10 0 0 1 512,416 L 512,456 L 512,570"/>
  <path class="flow cold" id="flowToSerp" d="M 320,570 L 320,456 L 320,416 A 10,10 0 0 1 341.33,416 L 341.33,456 A 10,10 0 0 0 362.67,456 L 362.67,416 A 10,10 0 0 1 384,416 L 384,456 A 10,10 0 0 0 405.33,456 L 405.33,416 A 10,10 0 0 1 426.67,416 L 426.67,456 A 10,10 0 0 0 448,456 L 448,416 A 10,10 0 0 1 469.33,416 L 469.33,456 A 10,10 0 0 0 490.67,456 L 490.67,416 A 10,10 0 0 1 512,416 L 512,456"/>
  <path class="flow hot" id="flowFromSerp" d="M 512,476 L 512,570"/>

  <g id="badgeT2">
    <circle cx="416" cy="396" r="10" fill="#0d1512" stroke="var(--amber)" stroke-width="2"/>
  </g>

  <!-- ===== JACUZZI ===== -->
  <g transform="translate(30,502.5)">
    <rect x="0" y="0" width="144" height="82" rx="8" fill="#0d1512" stroke="var(--line)" stroke-width="2"/>
    <rect class="tank-water" id="jacuzziWater" x="6" y="22" width="132" height="54" rx="5" fill="var(--water)"/>
    <circle id="badgeT1" cx="24" cy="64" r="9" fill="#0d1512" stroke="var(--amber)" stroke-width="2"/>
  </g>

  <!-- jacuzzi salida -> motor -->
  <path class="pipe" d="M 174,570 L 234,570"/>
  <path class="flow cold" id="flowToPump" d="M 174,570 L 234,570"/>

  <!-- ===== MOTOR ===== -->
  <g transform="translate(250,570)">
    <circle r="16" fill="#0d1512" stroke="var(--steel)" stroke-width="2.5"/>
    <g class="pump-blade" id="pumpBlade">
      <line x1="-9" y1="0" x2="9" y2="0" stroke="var(--amber)" stroke-width="2.5"/>
      <line x1="0" y1="-9" x2="0" y2="9" stroke="var(--amber)" stroke-width="2.5"/>
    </g>
  </g>

  <!-- motor -> valvula 1 (faltaba el flujo animado) -->
  <path class="pipe" d="M 266,570 L 304,570"/>
  <path class="flow cold" id="flowPumpToV1" d="M 266,570 L 304,570"/>

  <!-- ===== VALVULA 1 (nodo: recto a filtro / rama sube al serpentin) ===== -->
  <g class="valve" id="valve1" transform="translate(320,570)">
    <rect class="cap" x="-13" y="-7" width="26" height="14" rx="7"/>
    <line class="stem" x1="-7" y1="0" x2="7" y2="0"/>
  </g>

  <!-- valvula1 -> filtro (recto, filtro centrado entre V1 y V2) -->
  <path class="pipe" d="M 333,570 L 388,570"/>
  <path class="flow cold" id="flowV1ToFilter" d="M 333,570 L 388,570"/>

  <!-- ===== FILTRO (centrado entre V1 y V2) ===== -->
  <g transform="translate(388,507.5)">
    <path d="M0,65 L0,14 Q0,0 14,0 L42,0 Q56,0 56,14 L56,65 Z" fill="#1a2b25" stroke="var(--steel)" stroke-width="2"/>
    <rect id="filterSand" x="5" y="38" width="46" height="22" fill="#7a5a34" opacity=".55"/>
  </g>

  <!-- filtro -> valvula 2 (alineada con la tuberia V1->filtro) -->
  <path class="pipe" d="M 444,570 L 512,570"/>
  <path class="flow cold" id="flowFilterToV2" d="M 444,570 L 512,570"/>

  <!-- ===== VALVULA 2 (mismo giro 2D que V1, girada 90° como grupo para orientarse con la tuberia vertical) ===== -->
  <g class="valve closed" id="valve2" transform="translate(512,570) rotate(90)">
    <rect class="cap" x="-13" y="-7" width="26" height="14" rx="7"/>
    <line class="stem" x1="-7" y1="0" x2="7" y2="0"/>
  </g>

  <!-- retorno: valvula2 -> baja -> izquierda -> sube al jacuzzi -->
  <path class="pipe" d="M 512,584 L 512,634.5 L 110,634.5 L 110,584.5"/>
  <path class="flow cold" id="flowReturn" d="M 512,584 L 512,634.5 L 110,634.5 L 110,584.5"/>

  <!-- ===== CAPA DE TEXTOS: se pinta la última para quedar siempre por encima de tuberías y formas ===== -->
  <g id="textLayer">
    <text x="102" y="492.5" text-anchor="middle" class="lbl">JACUZZI · 1 m³</text>
    <text x="54" y="570" text-anchor="middle" class="badge" fill="var(--amber)" font-size="8">T1</text>
    <text x="54" y="594.5" text-anchor="middle" class="val" id="tempJacuzzi" font-size="14">— °C</text>

    <text x="416" y="399.5" text-anchor="middle" class="badge" fill="var(--amber)" font-size="8">T2</text>
    <text x="416" y="420" text-anchor="middle" class="val" id="tempSolar" font-size="14">— °C</text>

    <text x="250" y="602" text-anchor="middle" class="lbl">MOTOR</text>
    <text x="320" y="602" text-anchor="middle" class="lbl">V1</text>
    <text x="416" y="591.5" text-anchor="middle" class="lbl">FILTRO</text>
    <text x="512" y="606" text-anchor="middle" class="lbl">V2</text>
  </g>

</g>
</svg>
</div>

</div>

<script>
// -----------------------------------------------------------------------
// Cliente web real: NO hay simulacion. Todos los datos vienen del ESP32
// por WebSocket, y todos los comandos del usuario se envian al ESP32,
// que es quien decide, aplica y guarda el estado real del sistema.
// -----------------------------------------------------------------------
let state = null;           // ultimo estado recibido del ESP32 (null hasta la 1a llegada)
let clockOffsetMs = 0;      // diferencia entre la hora del ESP32 y el reloj local del navegador
let ws = null;

const el = id => document.getElementById(id);
const DIAS_LBL = ['D','L','M','X','J','V','S'];

// ---- Conexion WebSocket con el ESP32 ----
function connectWs(){
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (ev)=>{
    const data = JSON.parse(ev.data);
    state = data;
    clockOffsetMs = (data.clock*1000) - Date.now();
    render();
    renderSchedule();
  };
  ws.onclose = ()=> setTimeout(connectWs, 2000); // reintenta si se cae la conexion
}

function sendCmd(obj){
  if(ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

// ---- Color por temperatura: azul hasta 25°, degradado a rojo hasta 42°, parpadeo por encima de 42° ----
const BLUE = [47,166,201];
const RED  = [255,0,0];
function colorForTemp(temp){
  const k = Math.min(1, Math.max(0, (temp-25)/(42-25)));
  const r = Math.round(BLUE[0] + k*(RED[0]-BLUE[0]));
  const g = Math.round(BLUE[1] + k*(RED[1]-BLUE[1]));
  const b = Math.round(BLUE[2] + k*(RED[2]-BLUE[2]));
  return `rgb(${r},${g},${b})`;
}

function updateThermalColors(){
  if(!state) return;
  el('jacuzziWater').setAttribute('fill', colorForTemp(state.tJacuzzi));
  el('jacuzziWater').classList.toggle('hot-blink', state.tJacuzzi > 42);
  el('tempSolar').setAttribute('fill', colorForTemp(state.tSolar));
  el('tempSolar').classList.toggle('hot-blink-text', state.tSolar > 42);
}

function applyValve(id, open){
  el(id).classList.toggle('closed', !open);
}

function render(){
  if(!state) return;

  el('tempJacuzzi').textContent = state.tJacuzzi.toFixed(1)+' °C';
  el('tempSolar').textContent = state.tSolar.toFixed(1)+' °C';
  el('statT1').textContent = state.tJacuzzi.toFixed(1)+' °C';
  el('statT2').textContent = state.tSolar.toFixed(1)+' °C';
  el('offT1Val').textContent = (state.offsetT1>=0?'+':'')+state.offsetT1.toFixed(1)+' °C';
  el('offT2Val').textContent = (state.offsetT2>=0?'+':'')+state.offsetT2.toFixed(1)+' °C';
  el('statPump').textContent = state.pumpOn ? 'ON' : 'OFF';
  el('pillBomba').classList.toggle('bad', !state.pumpOn);

  el('segV1').querySelector('[data-v="filtro"]').classList.toggle('active', !state.valvulasActivas);
  el('segV1').querySelector('[data-v="serpentin"]').classList.toggle('active', state.valvulasActivas);
  el('segV2').querySelector('[data-v="cerrada"]').classList.toggle('active', !state.valvulasActivas);
  el('segV2').querySelector('[data-v="abierta"]').classList.toggle('active', state.valvulasActivas);

  applyValve('valve1', !state.valvulasActivas);
  applyValve('valve2', state.valvulasActivas);

  el('pumpBlade').classList.toggle('on', state.pumpOn);

  const solarActive  = state.pumpOn && state.valvulasActivas;
  const filterActive = state.pumpOn && !state.valvulasActivas;

  el('flowToPump').classList.toggle('on', state.pumpOn);
  el('flowPumpToV1').classList.toggle('on', state.pumpOn);
  el('flowV1ToFilter').classList.toggle('on', filterActive);
  el('flowFilterToV2').classList.toggle('on', filterActive);
  el('flowToSerp').classList.toggle('on', solarActive);
  el('flowFromSerp').classList.toggle('on', solarActive);
  el('flowReturn').classList.toggle('on', filterActive || solarActive);
  el('flowReturn').classList.toggle('hot', solarActive);
  el('flowReturn').classList.toggle('cold', !solarActive);

  const sand = el('filterSand');
  if(filterActive){ sand.setAttribute('fill', '#e8720f'); sand.setAttribute('opacity', '0.9'); }
  else if(solarActive){ sand.setAttribute('fill', '#6f7a76'); sand.setAttribute('opacity', '0.7'); }
  else { sand.setAttribute('fill', '#7a5a34'); sand.setAttribute('opacity', '0.55'); }

  let modeLabel = 'PARADO';
  if(state.pumpOn){
    modeLabel = solarActive ? 'CALENTANDO (SOLAR)' : 'FILTRANDO (NORMAL)';
  }
  if(state.dischargeActive) modeLabel = 'DESCARGA SOLAR';
  el('modeText').textContent = modeLabel;
  el('pillModo').classList.toggle('bad', !state.pumpOn && !state.dischargeActive);

  // Cuenta atras de la descarga forzada del serpentin solar (ventana de 5 min)
  el('dischargeBar').classList.toggle('on', !!state.dischargeActive);
  if(state.dischargeActive){
    const s = Math.max(0, state.dischargeRemainSec|0);
    const mm = String(Math.floor(s/60)).padStart(2,'0');
    const ss = String(s%60).padStart(2,'0');
    el('dischargeTime').textContent = mm+':'+ss;
  }

  // btnAuto y btnPump son interruptores independientes (ON/OFF propio).
  // btnForceSolar/btnForceBypass son mutuamente excluyentes entre si.
  el('btnAuto').classList.toggle('active', state.autoEnabled);
  el('btnPump').classList.toggle('active', state.pumpManual);
  el('btnForceSolar').classList.toggle('active', state.valvulasActivas);
  el('btnForceBypass').classList.toggle('active', !state.valvulasActivas);

  const locked = state.valvesLocked;
  ['btnForceSolar','btnForceBypass'].forEach(id => el(id).disabled = locked);

  updateThermalColors();
}

function estimatedNow(){
  return new Date(Date.now() + clockOffsetMs);
}

function pad2(n){ return String(n).padStart(2,'0'); }

function renderSchedule(){
  if(!state) return;
  const s = state.schedule;

  {
    const now = estimatedNow();
    el('statClock').textContent = now.toLocaleTimeString('es-ES');
    el('statClockDate').textContent = now.toLocaleDateString('es-ES');
  }
  const diasTxt = s.days.map((on,i)=> on ? DIAS_LBL[i] : null).filter(Boolean).join(' ');
  el('progText').textContent = `${pad2(s.startHour)}:${pad2(s.startMinute)} - ${pad2(s.endHour)}:${pad2(s.endMinute)}`;
  el('progDaysTxt').textContent = diasTxt;

  if(state.nextStart > 0){
    const next = new Date(state.nextStart*1000);
    const dia = next.toLocaleDateString('es-ES',{weekday:'short'});
    const diffMin = Math.max(0, Math.round((next-estimatedNow())/60000));
    const h = Math.floor(diffMin/60), m = diffMin%60;
    const countdown = h>0 ? `${h}h ${m}min` : `${m}min`;
    el('nextProgText').textContent = `${dia} ${pad2(next.getHours())}:${pad2(next.getMinutes())} · en ${countdown}`;
  } else {
    el('nextProgText').textContent = 'Sin programas configurados';
  }

  el('tempSetVal').textContent = state.targetTemp.toFixed(1)+' °C';
  el('solarDisVal').textContent = state.solarDischargeTemp.toFixed(1)+' °C';
}

// ---- Panel de edicion del programa de filtracion ----
let editDays = [];
function buildDiasPicker(){
  const cont = el('progDias');
  cont.innerHTML = '';
  DIAS_LBL.forEach((lbl, idx)=>{
    const d = document.createElement('div');
    d.className = 'dia'+(editDays[idx] ? ' on' : '');
    d.textContent = lbl;
    d.onclick = ()=>{ editDays[idx] = !editDays[idx]; d.classList.toggle('on'); };
    cont.appendChild(d);
  });
}

el('clockRow').onclick = ()=>{
  if(!state) return;
  el('progEdit').classList.remove('open'); // no dejar los dos paneles abiertos a la vez
  const open = el('clockEdit').classList.toggle('open');
  if(open){
    const now = estimatedNow();
    el('clockSet').value = now.toTimeString().slice(0,8);
    el('clockSetDate').value = `${now.getFullYear()}-${pad2(now.getMonth()+1)}-${pad2(now.getDate())}`;
  }
};

el('clockGuardar').onclick = ()=>{
  if(!el('clockSet').value || !el('clockSetDate').value) return;
  const [h,m,s] = el('clockSet').value.split(':').map(Number);
  const [y,mo,da] = el('clockSetDate').value.split('-').map(Number);
  const d = new Date(y, mo-1, da, h, m, s||0, 0);
  sendCmd({ cmd:'setClock', epoch: Math.floor(d.getTime()/1000) });
  el('clockEdit').classList.remove('open');
};

el('progRow').onclick = ()=>{
  if(!state) return;
  el('clockEdit').classList.remove('open'); // no dejar los dos paneles abiertos a la vez
  const open = el('progEdit').classList.toggle('open');
  if(open){
    const s = state.schedule;
    el('progStart').value = `${pad2(s.startHour)}:${pad2(s.startMinute)}`;
    el('progEnd').value   = `${pad2(s.endHour)}:${pad2(s.endMinute)}`;
    editDays = s.days.slice();
    buildDiasPicker();
  }
};

el('progGuardar').onclick = ()=>{
  const [sh, sm] = el('progStart').value.split(':').map(Number);
  const [eh, em] = el('progEnd').value.split(':').map(Number);

  sendCmd({ cmd:'setSchedule', startHour:sh, startMinute:sm, endHour:eh, endMinute:em, days:editDays });
  el('progEdit').classList.remove('open');
};

// ---- Temperatura objetivo, resolucion de 0.1°C ----
el('tempUp').onclick = ()=>{
  if(!state) return;
  sendCmd({ cmd:'setTargetTemp', value: +(state.targetTemp+0.1).toFixed(1) });
};
el('tempDown').onclick = ()=>{
  if(!state) return;
  sendCmd({ cmd:'setTargetTemp', value: +(state.targetTemp-0.1).toFixed(1) });
};

// ---- Limite de descarga solar (T2), solo informativo/guardado ----
el('solarDisUp').onclick = ()=>{
  if(!state) return;
  sendCmd({ cmd:'setSolarDischargeTemp', value: +(state.solarDischargeTemp+0.5).toFixed(1) });
};
el('solarDisDown').onclick = ()=>{
  if(!state) return;
  sendCmd({ cmd:'setSolarDischargeTemp', value: +(state.solarDischargeTemp-0.5).toFixed(1) });
};

// ---- Offset de calibracion de T1/T2, resolucion 0.5°C. Cada fila de
// temperatura del listado despliega su propio panel de ajuste al tocarla,
// y se cierra si se toca la misma fila o la otra ----
const ALL_TCARD_PANELS = ['offsetT1Ctrl','offsetT2Ctrl','tempObjCtrl','tempDisCtrl'];
function toggleOffsetPanel(panelId){
  const wasOpen = el(panelId).classList.contains('open');
  ALL_TCARD_PANELS.forEach(id => el(id).classList.remove('open'));
  el(panelId).classList.toggle('open', !wasOpen);
}
el('rowT1').onclick = ()=> toggleOffsetPanel('offsetT1Ctrl');
el('rowT2').onclick = ()=> toggleOffsetPanel('offsetT2Ctrl');
el('rowTempObj').onclick = ()=> toggleOffsetPanel('tempObjCtrl');
el('rowTempDis').onclick = ()=> toggleOffsetPanel('tempDisCtrl');
el('offsetT1Ctrl').onclick = (e)=> e.stopPropagation();
el('offsetT2Ctrl').onclick = (e)=> e.stopPropagation();
el('tempObjCtrl').onclick = (e)=> e.stopPropagation();
el('tempDisCtrl').onclick = (e)=> e.stopPropagation();

el('offT1Up').onclick   = ()=>{ if(state) sendCmd({ cmd:'setTempOffset', sensor:1, value: +(state.offsetT1+0.5).toFixed(1) }); };
el('offT1Down').onclick = ()=>{ if(state) sendCmd({ cmd:'setTempOffset', sensor:1, value: +(state.offsetT1-0.5).toFixed(1) }); };
el('offT2Up').onclick   = ()=>{ if(state) sendCmd({ cmd:'setTempOffset', sensor:2, value: +(state.offsetT2+0.5).toFixed(1) }); };
el('offT2Down').onclick = ()=>{ if(state) sendCmd({ cmd:'setTempOffset', sensor:2, value: +(state.offsetT2-0.5).toFixed(1) }); };

// ---- Botones: auto y bomba manual son interruptores independientes;
// forzar solar/filtro es un selector de 2 posiciones excluyentes ----
el('btnAuto').onclick        = ()=> sendCmd({ cmd:'setAuto', enabled: !state.autoEnabled });
el('btnPump').onclick        = ()=> sendCmd({ cmd:'togglePump' });
el('btnForceSolar').onclick  = ()=> sendCmd({ cmd:'setForceSolar', solar:true });
el('btnForceBypass').onclick = ()=> sendCmd({ cmd:'setForceSolar', solar:false });

// ---- Desplegable OPCIONES: muestra/oculta WIFI, DATOS, DIAGNOSTICO, RESTART ----
el('btnOpciones').onclick = (e)=>{
  e.stopPropagation();
  el('opcionesRow').classList.toggle('open');
  el('btnOpciones').classList.toggle('active');
};
// Cierra el desplegable si se pulsa fuera de el
document.addEventListener('click', (e)=>{
  if (!el('opcionesRow').contains(e.target) && e.target !== el('btnOpciones')) {
    el('opcionesRow').classList.remove('open');
    el('btnOpciones').classList.remove('active');
  }
});

// ---- Activar captive portal de configuracion WiFi (sin reiniciar) ----
// La gestion completa (añadir, priorizar, eliminar redes y guardar+salir
// reiniciando) se hace en la propia pagina del captive portal.
el('btnWifi').onclick = ()=>{
  sendCmd({ cmd:'activateWifiAp' });
  // Si ya estamos viendo la pagina desde el propio AP, vamos directos
  // al portal; si estamos en la red domestica, hay que avisar al
  // usuario para que cambie de red manualmente (no se puede hacer desde JS).
  if (location.hostname === '192.168.4.1') {
    location.href = 'http://192.168.4.1/wifi-config';
  } else {
    el('resetOverlay').classList.add('open');
  }
};

el('btnResetCancelar').onclick = ()=>{
  el('resetOverlay').classList.remove('open');
};

// ---- Reinicio del ESP32, con confirmacion para evitar pulsaciones accidentales ----
el('btnRestart').onclick = ()=>{
  if(confirm('¿Reiniciar el sistema ahora?')){
    sendCmd({ cmd:'restart' });
  }
};

// Reloj visual: se actualiza cada segundo interpolando localmente entre
// los estados reales que llegan por WebSocket (evita parpadeo de la hora)
setInterval(renderSchedule, 1000);

connectWs();
</script>
</body>
</html>


)HTMLPAGE";