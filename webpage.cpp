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
  --pipe:#2b3b36; --water:#00c8f0; --water-hot:#e0672e;
  --amber:#ffb020; --green:#2eff7a; --red:#ff3b2e; --text:#d8e2dd; --dim:#9db3a6;
  --mono:'Courier New',monospace;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--mono);padding:14px;}
h1{font-size:12px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;margin:0 0 8px 4px;}
.wrap{max-width:560px;margin:0 auto;height:calc(100vh - 28px);display:flex;flex-direction:column;overflow:hidden;}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:5px;margin-top:5px;}
.panel-main{flex:1;min-height:0;display:flex;flex-direction:column;overflow-y:auto;}
.panel-scheme{margin-top:auto;flex:0 0 auto;padding-top:8px;}
svg{width:100%;height:auto;display:block;}
.minibox{font-family:var(--mono);display:flex;flex-direction:column;gap:0;background:#0d1512;border:1px solid var(--line);border-radius:6px;padding:5px 5px;}
.minibox + .minibox{margin-top:5px;}
.cards{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:6px;}
.tcard{background:transparent;border:none;border-bottom:2px solid var(--amber);padding:10px 4px;text-align:left;cursor:default;position:relative;}
.tcard.clickable{cursor:pointer;}
.tcard-lbl{color:#ffffff;font-size:18px;font-weight:900;letter-spacing:.4px;}
.tcard-val{display:block;color:var(--amber);font-size:30px;font-weight:900;margin-top:2px;}
#rowT1 .tcard-lbl{color:#ff6b5e;} #rowT1 .tcard-val{color:#ff6b5e;}
.max-badge{fill:#ffffff;font-weight:700;font-family:var(--mono, monospace);}
#rowT2 .tcard-lbl{color:#4fd6ff;} #rowT2 .tcard-val{color:#4fd6ff;}
#rowTempObj .tcard-lbl{color:#ffb020;} #rowTempObj .tcard-val{color:#ffb020;}
#rowTempDis .tcard-lbl{color:#c78bff;} #rowTempDis .tcard-val{color:#c78bff;}
.minibox .row{display:flex;justify-content:space-between;align-items:center;font-size:14px;color:var(--dim);letter-spacing:.3px;padding:10px 0;border-bottom:1px solid #223229;}
.minibox .row:last-child{border-bottom:none;}
.minibox .row .lbl{color:#ffffff;font-weight:900;font-size:14px;letter-spacing:.4px;}
.minibox .row b{color:#ebed8f;font-weight:900;font-size:15px;}
.minibox .row.mode b{color:var(--green);font-size:16px;}
.discharge-bar{display:none;background:#1a1408;border:1px solid var(--amber);border-radius:6px;padding:8px 12px;margin-bottom:6px;text-align:center;}
.discharge-bar.on{display:block;animation:dischargeBlink 3s step-start infinite;}
.discharge-bar b{color:var(--amber);font-size:13px;letter-spacing:.5px;}
@keyframes dischargeBlink{0%{background:#1a1408;}0.1%{background:#5a0f0f;}33%{background:#5a0f0f;}33.1%{background:#1a1408;}}
.reject-blink{background:#c0392b!important;border-color:#c0392b!important;color:#fff!important;}
.minibox .row2{display:grid;grid-template-columns:1fr 1fr;align-items:center;padding:10px 0;border-bottom:1px solid #223229;}
.pillrow{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap;}
.pill{display:flex;align-items:center;gap:6px;font-size:14px;font-weight:700;padding:7px 14px;border-radius:22px;background:var(--green);color:#03140a;}
.pill.bad{background:var(--red);color:#1a0503;}
.pill .pilllbl{opacity:.8;}
.pill b{font-weight:900;}
.valverow{border-top:1px solid #22332c;padding-top:12px;display:flex;flex-direction:column;gap:10px;}
.valveitem{display:flex;justify-content:space-between;align-items:center;gap:8px;}
.vlbl{font-size:18px;font-weight:900;letter-spacing:.4px;color:#7fe8ff;}
.seg{display:flex;background:#0d1512;border:1px solid var(--line);border-radius:22px;overflow:hidden;}
.seg .opt{padding:6px 14px;font-size:12px;font-weight:700;color:var(--dim);white-space:nowrap;}
/* ---- Tarjeta viva: bomba + FILTRANDO/CALENTANDO + descarga + maniobra de valvulas ---- */
.lv-line{display:flex;align-items:center;gap:6px;flex-wrap:nowrap;padding:4px 0;}
.lv-pump{display:flex;align-items:center;gap:6px;flex:0 0 auto;font-weight:900;font-size:18px;letter-spacing:.4px;color:#fff;}
.lv-eq{display:flex;gap:3px;align-items:center;height:30px;flex:0 0 auto;}
.lv-eq i{width:6px;height:6px;background:var(--green);border-radius:3px;animation:lvEq .9s ease-in-out infinite;animation-play-state:paused;}
.lv-eq i:nth-child(2){animation-delay:.2s;}
.lv-eq i:nth-child(3){animation-delay:.4s;}
@keyframes lvEq{0%,100%{height:6px;}50%{height:28px;}}
.lv-pump.on .lv-eq i{animation-play-state:running;}
.lv-pump.off span{color:var(--red);}
.lv-pump.off .lv-eq i{background:var(--red);}
.lv-opt{flex:1 1 0;min-width:0;display:flex;align-items:center;justify-content:center;gap:6px;height:44px;padding:0 6px;border-radius:8px;border:1px solid var(--line);background:#0d1512;opacity:.35;transition:opacity .3s,border-color .3s;font-size:18px;font-weight:900;letter-spacing:.4px;color:#fff;white-space:nowrap;}
.lv-opt.c .lv-eq i{background:#00c8f0;}
.lv-opt.o .lv-eq i{background:#e0672e;}
.lv-opt.c{color:#4fd6ff;}
.lv-opt.o{color:#ff6b5e;}
.lv-opt.act{opacity:1;}
.lv-opt.c.act{border-color:#00c8f0;}
.lv-opt.o.act{border-color:#e0672e;}
.lv-opt.act .lv-eq i{animation-play-state:running;}
.lv-opt.tgt{opacity:1;border-color:var(--amber);animation:lvPl .8s infinite;}
@keyframes lvPl{50%{opacity:.35;}}
.lv-row{display:none;position:relative;margin-top:8px;height:34px;border-radius:8px;overflow:hidden;border:1px solid var(--amber);background:#2a1f08;}
.lv-row.on{display:block;}
.lv-mv{background:#1a1408;}
.lv-fill{position:absolute;left:0;top:0;bottom:0;width:0;background:#8a5f0f;transition:width 1s linear;}
.lv-tx{position:absolute;left:0;right:0;top:0;bottom:0;padding:0 10px;display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:12px;color:#fff;white-space:nowrap;}
.lv-tx b{font-weight:900;}
.clockprogrow{display:flex;flex-direction:column;}
.clockbox,.progbox{background:transparent;padding:6px 6px 4px;}
.clockbox.clickable,.progbox.clickable{cursor:pointer;}
.clockbox.clickable:hover .cb-lbl,.progbox.clickable:hover .cb-lbl{color:var(--amber);}
.progbox{border-top:1px solid #223229;margin-top:8px;padding-top:12px;}
.progbox .cb-lbl{flex-wrap:wrap;row-gap:6px;}
.cb-head{display:flex;align-items:center;gap:10px;}
.cb-lbl{font-size:13px;font-weight:900;letter-spacing:1.5px;color:var(--dim);display:flex;align-items:center;gap:6px;}
.cb-time{display:block;font-size:30px;font-weight:900;color:#7fe8ff;line-height:1;}
.cb-sec{color:#4f8d9b;}
.cb-date{display:block;font-size:17px;font-weight:900;letter-spacing:.4px;color:var(--dim);text-align:right;line-height:1.5;margin-left:auto;white-space:nowrap;}
.tl{position:relative;height:74px;margin-top:4px;}
.tl-track{position:absolute;left:0;right:0;top:30px;height:12px;border-radius:6px;background:#1b2622;}
.tl-win{position:absolute;top:30px;height:12px;border-radius:6px;background:linear-gradient(90deg,#2eff7a,#16b8a0);display:none;}
.tl-tick{position:absolute;top:43px;width:1px;height:5px;background:#3a4a44;}
.tl-now{position:absolute;top:21px;width:2px;height:30px;background:#7fe8ff;transform:translateX(-50%);}
.tl-nowlbl{position:absolute;top:0;transform:translateX(-50%);font-size:11px;font-weight:900;letter-spacing:1px;color:#7fe8ff;}
.tl-edge{position:absolute;top:54px;transform:translateX(-50%);font-size:12px;font-weight:900;color:#8fffb0;white-space:nowrap;}
.pchips{display:flex;gap:3px;margin-left:auto;}
.pchip{width:22px;height:22px;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:900;letter-spacing:0;border-radius:6px;border:1px solid #22332c;color:#5d7268;}
.pchip.on{background:rgba(46,255,122,.12);border-color:rgba(46,255,122,.4);color:#8fffb0;}
.pchip.hoy{outline:1.5px solid #7fe8ff;outline-offset:1px;}
.tl-dur{font-size:12px;font-weight:900;letter-spacing:1px;color:var(--dim);}
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
#tempDisCtrl{flex-wrap:wrap;row-gap:10px;}
.dur-ctrl{flex:0 0 100%;width:100%;text-align:center;border-top:1px solid var(--line);padding-top:10px;}
.dur-ctrl .dur-lbl{font-size:10px;letter-spacing:1px;color:var(--dim);}
.dur-ctrl .dur-val{display:block;font-size:14px;color:var(--amber);margin-top:4px;}
.dur-ctrl input[type=range]{width:100%;margin:8px 0 0;accent-color:var(--amber);background:transparent;}
.dur-ctrl .dur-mm{display:flex;justify-content:space-between;font-size:9px;color:var(--dim);}
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
.reset-box button.confirmar{background:#2eff7a;color:#0b1210;border:none;}
.reset-ssid{font-size:18px;color:var(--green);font-weight:bold;}
#resetLinkManual{display:inline-block;margin-top:10px;color:var(--amber);}
.led{transition:fill .4s, opacity .4s;}
.led.off{fill:#2a332e;opacity:.6;}
.pump-blade{transform-box:fill-box;transform-origin:center;}
.pump-blade.on{animation:spin 0.6s linear infinite;}
@keyframes spin{to{transform:rotate(360deg);}}
.sun-ray{stroke:#ffb020;stroke-width:2;opacity:0;animation:ray 2.4s ease-in-out infinite;}
@keyframes ray{0%{opacity:0;}30%{opacity:.55;}100%{opacity:0;}}
.statusbar{display:flex;gap:14px;flex-wrap:wrap;margin-top:8px;padding:10px 14px;background:#0d1512;border:1px solid var(--line);border-radius:6px;font-size:12px;}
.statusbar .item{display:flex;align-items:center;gap:6px;color:var(--dim);}
@media (max-width:480px){
  .tcard-lbl{font-size:12px;}
  .tcard-val{font-size:19px;}
  .minibox .row{font-size:11px;padding:6px 0;}
  .minibox .row .lbl{font-size:11px;}
  .minibox .row b{font-size:12px;}
  .minibox .row.mode b{font-size:12px;}
  .minibox .row2 .cell{font-size:11px;}
  .minibox .row2 .cell .lbl{font-size:11px;}
  .minibox .row2 .cell b{font-size:12px;}
  .minibox .row2 .cell.mode b{font-size:12px;}
  .cb-lbl{font-size:11px;}
  .cb-time{font-size:24px;}
  .cb-date{font-size:14px;}
  .tl-edge{font-size:11px;}
  .vlbl{font-size:14px;}
  .pill{font-size:11px;padding:5px 10px;}
  .lv-pump,.lv-opt{font-size:12px;}
  .lv-eq{height:22px;}
}
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
      <button onclick="location.href='/leds'">LEDS</button>
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
<div class="panel panel-main">
    <div class="minibox">


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
            <div class="dur-ctrl">
              <div class="dur-lbl">DURACION DE DESCARGA</div>
              <b class="dur-val" id="durVal">—</b>
              <input type="range" id="durSlider" min="1" max="15" step="1" value="5">
              <div class="dur-mm"><span>1</span><span>15 min</span></div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="minibox">
      <div class="lv-line">
        <div class="lv-pump" id="lvPump"><div class="lv-eq"><i></i><i></i><i></i></div><span>BOMBA</span></div>
        <div class="lv-opt c" id="lvFilt"><div class="lv-eq"><i></i><i></i><i></i></div><span>FILTRANDO</span></div>
        <div class="lv-opt o" id="lvCal"><div class="lv-eq"><i></i><i></i><i></i></div><span>CALENTANDO</span></div>
      </div>
      <div class="lv-row" id="lvCd"><i class="lv-fill" id="lvCdFill"></i><div class="lv-tx"><b>DESCARGA · <span id="lvCdTime">—</span></b><span>termina <span id="lvCdEnd">—</span></span></div></div>
      <div class="lv-row lv-mv" id="lvMv"><i class="lv-fill" id="lvMvFill"></i><div class="lv-tx"><b id="lvMvLbl">MOVIENDO VÁLVULAS</b><b id="lvMvSec">—</b></div></div>
    </div>

    <div class="minibox">
      <div class="clockprogrow">
        <div class="clockbox clickable" id="clockRow">
          <div class="cb-head">
            <b class="cb-time" id="statClock">—</b>
            <svg class="gear-icon" viewBox="0 0 24 24" fill="none" stroke="#7fe8ff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
            <span class="cb-date" id="statClockDate">—</span>
          </div>
        </div>
        <div class="progbox clickable" id="progRow">
          <div class="cb-lbl">PROGRAMA
            <svg class="gear-icon" viewBox="0 0 24 24" fill="none" stroke="#7fe8ff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
            <span class="tl-dur" id="progDur">—</span>
            <div class="pchips" id="progChips"></div>
          </div>
          <div class="tl" id="progTl">
            <span class="tl-nowlbl" id="tlNowLbl">AHORA</span>
            <div class="tl-track"></div>
            <i class="tl-win" id="tlWinA"></i>
            <i class="tl-win" id="tlWinB"></i>
            <i class="tl-now" id="tlNow"></i>
            <span class="tl-edge" id="tlStartLbl">—</span>
            <span class="tl-edge" id="tlEndLbl">—</span>
          </div>
        </div>
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

    <div class="panel-scheme">
<svg viewBox="5 375 545 270" xmlns="http://www.w3.org/2000/svg">

<g>

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
    <!-- Termometro vertical MAX. HOY (T1): tubo, escala y liquido degradado se generan por JS (buildGauge); el puntero, el valor y la etiqueta se mueven con el maximo (updateMaxGauge) -->
    <g id="t1GaugeZones"></g>
    <path id="t1GaugePtr" d="" style="display:none"/>
    <text x="90" y="435" text-anchor="start" class="max-badge" id="maxT1Badge" font-size="26">—</text>
    <text x="90" y="451" text-anchor="start" class="lbl" id="maxT1Lbl">MAX. HOY</text>
    <text x="54" y="570" text-anchor="middle" class="badge" fill="var(--amber)" font-size="8">T1</text>

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

// Hace parpadear en rojo un boton 5 veces (usado cuando se rechaza un
// comando manual por estar el modo automatico activo). Reutilizable con
// cualquier id de boton.
function blinkReject(buttonId){
  const btn = el(buttonId);
  if (!btn || btn.dataset.blinking === '1') return; // evita solaparse si se pulsa varias veces
  btn.dataset.blinking = '1';
  let count = 0;
  const iv = setInterval(()=>{
    btn.classList.toggle('reject-blink');
    count++;
    if (count >= 10) { // 10 toggles = 5 parpadeos completos (on/off)
      clearInterval(iv);
      btn.classList.remove('reject-blink');
      btn.dataset.blinking = '0';
    }
  }, 150);
}
const DIAS_LBL = ['D','L','M','X','J','V','S'];
const DIAS_ORDEN = [1,2,3,4,5,6,0]; // orden visual L M X J V S D (los datos del ESP32 siguen 0 = domingo)

// ---- Conexion WebSocket con el ESP32 ----
function connectWs(){
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (ev)=>{
    const data = JSON.parse(ev.data);
    if (data.reject === 'forceSolar') {
      // Comando SOLAR/FILTRACION ignorado por estar en modo automatico:
      // no es un estado nuevo, solo avisa a este mismo cliente.
      blinkReject(data.solar ? 'btnForceSolar' : 'btnForceBypass');
      return;
    }
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

// ---- Temperatura maxima del dia para T1 (Jacuzzi) ----
// Sale del historico del servidor (/api/history, la misma fuente que /datos):
// es el T1 mas alto registrado HOY (dia natural local). null = aun no hay
// muestras de hoy. Se refresca cada 5 min (ver fetchDailyMax).
let dailyMaxT1 = null;

// ---- Termometro vertical MAX. HOY (T1) ----
// Escala 20-50 C sobre un tubo vertical. El liquido usa un degradado continuo
// (cian frio -> verde -> ambar -> rojo) anclado a la escala absoluta, de modo
// que el color de la punta ya indica lo caliente que ha llegado el agua.
const G_TX = 64, G_TW = 13;          // centro X y ancho del tubo
const G_TOP = 398, G_BOT = 457;      // Y de 50 C (arriba) y de 20 C (abajo)
const G_BULB_Y = 467;                // centro Y del bulbo
const G_MIN = 20, G_MAX = 50;
const G_STOPS = [                    // [temperatura, [r,g,b]]
  [20, [0, 200, 240]],
  [30, [46, 255, 122]],
  [36, [255, 176, 32]],
  [50, [255, 59, 46]]
];

// Color del degradado para una temperatura "v" (se limita a 20-50 C).
function gaugeColor(v){
  v = Math.min(G_MAX, Math.max(G_MIN, v));
  for(let i = 1; i < G_STOPS.length; i++){
    const a = G_STOPS[i-1], b = G_STOPS[i];
    if(v <= b[0]){
      const k = (v - a[0]) / (b[0] - a[0]);
      return 'rgb(' + a[1].map((c, j)=> Math.round(c + (b[1][j] - c) * k)).join(',') + ')';
    }
  }
  return 'rgb(' + G_STOPS[G_STOPS.length-1][1].join(',') + ')';
}

// Coordenada Y (SVG) de una temperatura "v" sobre el tubo.
function gaugeY(v){
  const f = Math.min(1, Math.max(0, (v - G_MIN) / (G_MAX - G_MIN)));
  return G_BOT - f * (G_BOT - G_TOP);
}

// Dibuja tubo, bulbo, degradado y escala (se llama una sola vez al cargar).
function buildGauge(){
  const NS = 'http://www.w3.org/2000/svg';
  const g = el('t1GaugeZones');
  const mk = (tag, attrs, parent)=>{
    const n = document.createElementNS(NS, tag);
    for(const k in attrs) n.setAttribute(k, attrs[k]);
    (parent || g).appendChild(n);
    return n;
  };
  // Degradado vertical fijo a la escala (userSpaceOnUse)
  const lg = mk('linearGradient', { id:'thermoGrad', gradientUnits:'userSpaceOnUse',
                                    x1:0, y1:G_BOT, x2:0, y2:G_TOP }, mk('defs', {}));
  G_STOPS.forEach(st=> mk('stop', { offset:(st[0]-G_MIN)/(G_MAX-G_MIN),
                                    'stop-color':'rgb(' + st[1].join(',') + ')' }, lg));
  // Tubo y bulbo (fondo)
  mk('rect', { x:G_TX - G_TW/2, y:G_TOP, width:G_TW, height:G_BULB_Y - G_TOP, rx:G_TW/2, fill:'#1b2622' });
  mk('circle', { cx:G_TX, cy:G_BULB_Y, r:10, fill:'#1b2622' });
  // Liquido (su Y y alto los fija updateMaxGauge) y bulbo interior
  mk('rect', { id:'t1ThermoLiq', x:G_TX - 4, y:G_BOT, width:8, height:0, rx:4,
               fill:'url(#thermoGrad)', style:'display:none' });
  mk('circle', { cx:G_TX, cy:G_BULB_Y, r:7, fill:'rgb(' + G_STOPS[0][1].join(',') + ')' });
  // Escala: marca cada 5 C, etiqueta cada 10 C
  for(let t = G_MIN; t <= G_MAX; t += 5){
    const y = gaugeY(t), x2 = G_TX - G_TW/2 - 3, x1 = x2 - (t % 10 ? 4 : 7);
    mk('line', { x1:x1, y1:y.toFixed(1), x2:x2, y2:y.toFixed(1), stroke:'#3a4a44', 'stroke-width':1.5 });
    if(t % 10 === 0){
      const tx = mk('text', { x:x1 - 4, y:(y + 4).toFixed(1), 'text-anchor':'end', 'font-size':11,
                              class:'lbl', style:'fill:var(--dim)' });
      tx.textContent = t + '°';
    }
  }
}

// Pinta liquido, puntero y valor con el maximo de hoy (dailyMaxT1).
// El valor y la etiqueta siguen la altura del puntero. Sin datos: muestra
// "—" centrado y oculta liquido y puntero.
function updateMaxGauge(){
  const has = (dailyMaxT1 !== null);
  const ptr = el('t1GaugePtr'), liq = el('t1ThermoLiq');
  const badge = el('maxT1Badge'), lbl = el('maxT1Lbl');
  badge.textContent = has ? dailyMaxT1.toFixed(1) + '°' : '—';
  ptr.style.display = has ? '' : 'none';
  liq.style.display = has ? '' : 'none';
  const y = has ? gaugeY(dailyMaxT1) : (G_TOP + G_BOT) / 2;
  badge.setAttribute('y', (y + 9).toFixed(1));
  lbl.setAttribute('y', (y + 25).toFixed(1));
  if(!has) return;
  liq.setAttribute('y', y.toFixed(1));
  liq.setAttribute('height', (G_BULB_Y - y).toFixed(1));
  const px = G_TX + G_TW/2 + 5;
  ptr.setAttribute('d', 'M' + px + ' ' + y.toFixed(1) + 'L' + (px + 8) + ' ' + (y - 5).toFixed(1) +
                        'L' + (px + 8) + ' ' + (y + 5).toFixed(1) + 'Z');
  ptr.setAttribute('fill', gaugeColor(dailyMaxT1));
}

// Pide el historico y guarda el T1 mas alto de las muestras de hoy.
// Muestras: [ts, t1, t2, flags]. Si falla la peticion se conserva el
// ultimo valor conocido.
async function fetchDailyMax(){
  try{
    const res = await fetch('/api/history');
    const data = await res.json();
    const d0 = new Date(); d0.setHours(0,0,0,0);
    const from = d0.getTime()/1000;          // inicio del dia local (segundos)
    let mx = null;
    (data.samples || []).forEach(s=>{
      if(s[0] >= from && (mx === null || s[1] > mx)) mx = s[1];
    });
    dailyMaxT1 = mx;
    updateMaxGauge();
  }catch(e){ /* sin servidor: se mantiene el ultimo valor */ }
}

function render(){
  if(!state) return;

  el('tempSolar').textContent = state.tSolar.toFixed(1)+' °C';
  el('statT1').textContent = state.tJacuzzi.toFixed(1)+' °C';
  el('statT2').textContent = state.tSolar.toFixed(1)+' °C';
  el('offT1Val').textContent = (state.offsetT1>=0?'+':'')+state.offsetT1.toFixed(1)+' °C';
  el('offT2Val').textContent = (state.offsetT2>=0?'+':'')+state.offsetT2.toFixed(1)+' °C';

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

  // ---- Tarjeta viva: bomba, modo activo, cuenta atras de descarga y maniobra de valvulas ----
  {
    const pumpOn = !!state.pumpOn, locked = !!state.valvesLocked, sol = !!state.valvulasActivas;
    el('lvPump').classList.toggle('on', pumpOn);
    el('lvPump').classList.toggle('off', !pumpOn);
    // El modo solo se enciende con la bomba en marcha y las valvulas quietas;
    // durante la maniobra parpadea el modo de destino.
    el('lvFilt').classList.toggle('act', pumpOn && !sol && !locked);
    el('lvCal').classList.toggle('act',  pumpOn &&  sol && !locked);
    el('lvFilt').classList.toggle('tgt', locked && !sol);
    el('lvCal').classList.toggle('tgt',  locked &&  sol);

    // Fila de maniobra de valvulas (solo mientras giran)
    el('lvMv').classList.toggle('on', locked);
    if(locked){
      const mvTotal = state.valveMoveSec || 10;
      const rem = Math.max(0, state.valveRemainSec|0);
      el('lvMvLbl').textContent = sol ? 'MOVIENDO A SERPENTÍN' : 'MOVIENDO A FILTRO';
      el('lvMvSec').textContent = rem+' s';
      el('lvMvFill').style.width = Math.min(100, Math.max(0, (1 - rem/mvTotal)*100))+'%';
    }

    // Fila de descarga solar: cuenta atras, hora de fin y relleno de progreso
    el('lvCd').classList.toggle('on', !!state.dischargeActive);
    if(state.dischargeActive){
      const s = Math.max(0, state.dischargeRemainSec|0);
      const total = (state.dischargeMinutes || 5) * 60;
      const end = new Date(estimatedNow().getTime() + s*1000);
      el('lvCdTime').textContent = pad2(Math.floor(s/60))+':'+pad2(s%60);
      el('lvCdEnd').textContent = pad2(end.getHours())+':'+pad2(end.getMinutes());
      el('lvCdFill').style.width = Math.min(100, Math.max(0, (1 - s/total)*100))+'%';
    }
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

// Marcas horarias (cada 3 h) bajo la barra de la linea de tiempo. Una sola vez.
function buildTimelineTicks(){
  const tl = el('progTl');
  for(let h = 0; h <= 24; h += 3){
    const t = document.createElement('i');
    t.className = 'tl-tick';
    t.style.left = (h / 24 * 100) + '%';
    tl.appendChild(t);
  }
}

// Linea de tiempo de 24 h del programa: franja de filtracion (verde), marcador
// de la hora actual, etiquetas de inicio/fin, chips de dias y duracion.
// Soporta franjas que cruzan medianoche (dos barras). "s" = state.schedule.
let chipsSig = '';
function renderProgTimeline(s){
  const now = estimatedNow();
  const pct = m => m / 1440 * 100;
  const a = s.startHour * 60 + s.startMinute;
  const b = s.endHour * 60 + s.endMinute;
  const clamp = p => Math.min(92, Math.max(8, p));
  const setWin = (e, from, to)=>{
    e.style.display = 'block';
    e.style.left = from + '%';
    e.style.width = (to - from) + '%';
  };
  const winA = el('tlWinA'), winB = el('tlWinB');
  const ls = el('tlStartLbl'), le = el('tlEndLbl');
  const fmt = (h, m)=> pad2(h) + ':' + pad2(m);

  // Franja: normal (una barra), que cruza medianoche (dos) o vacia (inicio = fin)
  winB.style.display = 'none';
  if(a === b){
    winA.style.display = 'none';
    ls.style.display = 'none'; le.style.display = 'none';
  } else {
    if(a < b){ setWin(winA, pct(a), pct(b)); }
    else     { setWin(winA, pct(a), 100); setWin(winB, 0, pct(b)); }
    // Etiquetas de inicio/fin; si quedan muy juntas se funden en una sola
    const ps = pct(a), pe = pct(b);
    if(Math.abs(ps - pe) < 18){
      ls.textContent = fmt(s.startHour, s.startMinute) + '–' + fmt(s.endHour, s.endMinute);
      ls.style.left = clamp((ps + pe) / 2) + '%';
      ls.style.display = ''; le.style.display = 'none';
    } else {
      ls.textContent = fmt(s.startHour, s.startMinute); ls.style.left = clamp(ps) + '%';
      le.textContent = fmt(s.endHour, s.endMinute);     le.style.left = clamp(pe) + '%';
      ls.style.display = ''; le.style.display = '';
    }
  }

  // Marcador de la hora actual
  const nowPct = pct(now.getHours() * 60 + now.getMinutes() + now.getSeconds() / 60);
  el('tlNow').style.left = nowPct + '%';
  el('tlNowLbl').style.left = clamp(nowPct) + '%';

  // Chips de dias (solo se reconstruyen si cambian los dias o el dia de hoy)
  const hoy = now.getDay();
  const sig = s.days.join('') + '|' + hoy;
  if(sig !== chipsSig){
    chipsSig = sig;
    el('progChips').innerHTML = DIAS_ORDEN.map(i=>
      '<span class="pchip' + (s.days[i] ? ' on' : '') + (i === hoy ? ' hoy' : '') + '">' + DIAS_LBL[i] + '</span>').join('');
  }

  // Duracion de la franja (0 = inicio igual a fin: nunca se activa)
  const dur = (b - a + 1440) % 1440;
  el('progDur').textContent = dur ? (Math.floor(dur / 60) + ' h' + (dur % 60 ? ' ' + (dur % 60) + ' min' : '')) : '—';
}

function renderSchedule(){
  if(!state) return;
  const s = state.schedule;

  {
    const now = estimatedNow();
    el('statClock').innerHTML = pad2(now.getHours()) + ':' + pad2(now.getMinutes()) +
      '<span class="cb-sec">:' + pad2(now.getSeconds()) + '</span>';
    const wd = now.toLocaleDateString('es-ES', { weekday:'short' }).replace('.', '').toUpperCase();
    el('statClockDate').textContent = wd + ' ' + pad2(now.getDate()) + '/' + pad2(now.getMonth() + 1) + '/' + now.getFullYear();
  }
  renderProgTimeline(s);

  el('tempSetVal').textContent = state.targetTemp.toFixed(1)+' °C';
  el('solarDisVal').textContent = state.solarDischargeTemp.toFixed(1)+' °C';
  // Slider de duracion de descarga: no se toca mientras el usuario lo arrastra
  if(!durDragging && state.dischargeMinutes !== undefined){
    el('durSlider').value = state.dischargeMinutes;
    el('durVal').textContent = state.dischargeMinutes+' min';
  }
}

// ---- Panel de edicion del programa de filtracion ----
let editDays = [];
function buildDiasPicker(){
  const cont = el('progDias');
  cont.innerHTML = '';
  DIAS_ORDEN.forEach(idx=>{
    const d = document.createElement('div');
    d.className = 'dia'+(editDays[idx] ? ' on' : '');
    d.textContent = DIAS_LBL[idx];
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

// ---- Duracion de la descarga solar (slider 1-15 min). Mientras se arrastra
// solo se actualiza el numero; el valor se envia (y se guarda en el ESP32)
// al soltar, para no escribir en la flash a cada paso. ----
let durDragging = false;
el('durSlider').addEventListener('input', ()=>{
  durDragging = true;
  el('durVal').textContent = el('durSlider').value+' min';
});
el('durSlider').addEventListener('change', ()=>{
  sendCmd({ cmd:'setDischargeMinutes', value: parseInt(el('durSlider').value, 10) });
  durDragging = false;
});

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

buildGauge();
buildTimelineTicks();
updateMaxGauge();
fetchDailyMax();
setInterval(fetchDailyMax, 300000); // refresca el maximo del dia cada 5 min

connectWs();
</script>
</body>
</html>


)HTMLPAGE";