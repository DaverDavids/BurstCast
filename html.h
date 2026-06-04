#pragma once
// ============================================================
//  html.h — web UI
// ============================================================
#include <Arduino.h>
#include "config.h"

static const char CAPTIVE_FORM[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>BurstCast Setup</title>
<style>body{font-family:monospace;background:#111;color:#0f0;display:flex;justify-content:center;padding:2em}
form{display:flex;flex-direction:column;gap:.8em;width:300px}
input{background:#000;color:#0f0;border:1px solid #0f0;padding:.4em;font-family:monospace}
button{background:#0f0;color:#000;border:none;padding:.5em;cursor:pointer;font-weight:bold}</style></head>
<body><form method='POST' action='/savewifi'>
<h2>BurstCast WiFi Setup</h2>
<label>SSID<input name='ssid' required></label>
<label>Password<input name='pass' type='password'></label>
<button type='submit'>Save &amp; Reboot</button>
</form></body></html>
)rawhtml";

// Helper: emit a labeled range slider with a live value readout.
// name   = form field name & element id prefix
// label  = display label
// mn/mx  = min/max
// val    = current value (int)
// full   = true -> full width (single col), false -> used inside .row grid
static void addSlider(String& h, const char* name, const char* label,
                      int mn, int mx, int val) {
  h += "<label>";
  h += label;
  h += " <span id='v_";
  h += name;
  h += "' style='color:#0f0'>";
  h += val;
  h += "</span><br>"
       "<input type='range' name='";
  h += name;
  h += "' id='";
  h += name;
  h += "' min='";
  h += mn;
  h += "' max='";
  h += mx;
  h += "' value='";
  h += val;
  h += "' oninput=\"document.getElementById('v_";
  h += name;
  h += "').textContent=this.value\" style='width:100%'></label>";
}

String buildConfigPage(bool camOk) {
  uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
  static const uint16_t FW[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
  static const uint16_t FH[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

  String h;
  h.reserve(9000);

  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>BurstCast</title>"
         "<style>"
         "body{font-family:monospace;background:#111;color:#ccc;margin:0;padding:1em}"
         "h1{color:#0f0;margin:0 0 .5em}h2{color:#0a0;font-size:.9em;margin:1em 0 .4em}"
         ".card{background:#1a1a1a;border:1px solid #333;border-radius:6px;padding:1em;margin-bottom:1em;max-width:600px}"
         "label{display:block;margin:.4em 0;font-size:.85em}"
         "input[type=text],input[type=number],input[type=password],select{"
         "  background:#000;color:#0f0;border:1px solid #444;padding:.3em .5em;"
         "  font-family:monospace;width:100%;box-sizing:border-box}"
         "input[type=range]{accent-color:#0f0;cursor:pointer}"
         "input[type=checkbox]{accent-color:#0f0;width:auto;cursor:pointer}"
         "input[type=password]{letter-spacing:.1em}"
         ".row{display:grid;grid-template-columns:1fr 1fr;gap:.5em}"
         "button{background:#0f0;color:#000;border:none;padding:.5em 1.2em;cursor:pointer;font-weight:bold;margin-top:.5em}"
         "button.sec{background:#444;color:#ccc}"
         "button.danger{background:#f00;color:#fff}"
         ".status{display:inline-block;padding:.2em .6em;border-radius:3px;font-size:.8em;font-weight:bold}"
         ".ok{background:#0a0;color:#0f0}.fail{background:#500;color:#f00}"
         ".hint{color:#555;font-size:.78em;margin-top:.15em}"
         "#log{background:#000;border:1px solid #333;padding:.5em;height:80px;overflow-y:auto;font-size:.75em;color:#888}"
         // Collapsible sensor section
         "details{margin-top:.5em}"
         "details summary{cursor:pointer;color:#0a0;font-size:.85em;user-select:none;padding:.2em 0;list-style:none}"
         "details summary::-webkit-details-marker{display:none}"
         "details summary::before{content:'\u25b6  ';font-size:.7em}"
         "details[open] summary::before{content:'\u25bc  ';font-size:.7em}"
         ".sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:.5em;margin-top:.5em}"
         ".sensor-full{grid-column:1/-1}"
         ".manual-row{margin-top:.3em;padding:.3em .5em;background:#111;border-left:2px solid #333;font-size:.82em}"
         "</style></head><body>");

  h += F("<h1>&#127909; BurstCast</h1>");

  // Status bar
  h += F("<div class='card'>");
  h += "<span class='status ";
  h += camOk ? "ok'>Cam OK" : "fail'>Cam FAIL";
  h += "</span> ";
  h += "<span id='bstatus' class='status sec'>Idle</span> ";
  h += "<span id='rssi' style='font-size:.8em;color:#555'></span>";
  h += F("</div>");

  // Camera preview
  h += F("<div class='card'><h2>Preview</h2>"
         "<img id='prev' src='/cam.jpg' style='width:100%;max-width:320px;border:1px solid #333'>"
         "<br><button class='sec' onclick=\"document.getElementById('prev').src='/cam.jpg?t='+Date.now()\">Refresh</button>"
         "&nbsp;<button class='sec' onclick=\"window.open('/stream','_blank')\">Live Stream</button></div>");

  // Trigger
  h += F("<div class='card'><h2>Trigger</h2>"
         "<button onclick=\"fetch('/trigger').then(r=>r.json()).then(j=>alert(JSON.stringify(j)))\">&#9654; Trigger Burst</button>"
         "&nbsp;<button class='sec' onclick=\"fetch('/clearbuffer').then(()=>alert('Buffer cleared'))\">Clear Buffer</button></div>");

  // OBS WebSocket
  h += F("<form id='cfg'><div class='card'><h2>OBS WebSocket</h2>"
         "<label>OBS IP<input name='obsWsIp' value='");
  h += cfg.obsWsIp;
  h += F("'></label><div class='row'>"
         "<label>WS Port<input name='obsWsPort' type='number' value='");
  h += cfg.obsWsPort;
  h += F("'></label>"
         "<label>Password<input name='obsWsPass' type='password' value='");
  h += cfg.obsWsPass;
  h += F("'></label></div>"
         "<label>Scene Name<input name='obsSceneName' value='");
  h += cfg.obsSceneName;
  h += F("'></label>"
         "<label>Source Name (in OBS)<input name='obsSourceName' value='");
  h += cfg.obsSourceName;
  h += F("'></label>"
         "<div class='row'>"
         "<label>Source Visible (sec)<input name='visibleSecs' type='number' min='0' max='3600'"
         " id='visSecs' value='");
  h += cfg.visibleSecs;
  h += F("'><div class='hint' id='visHint'></div></label>"
         "<div style='padding-top:1.4em;font-size:.8em;color:#555'>0 = auto (matches clip length)</div>"
         "</div></div>");

  // Camera settings
  h += F("<div class='card'><h2>Camera</h2><div class='row'>"
         "<label>Frame Size<select name='frameSize'>");
  const char* sizeNames[] = {
    "QQVGA 160x120","QCIF 176x144","HQVGA 240x176","240x240",
    "QVGA 320x240","CIF 400x296","HVGA 480x320","VGA 640x480",
    "SVGA 800x600","XGA 1024x768","HD 1280x720","SXGA 1280x1024","UXGA 1600x1200"
  };
  for (int i = 0; i < 13; i++) {
    h += "<option value='";
    h += i;
    if (i == cfg.frameSize) h += "' selected>";
    else h += "'>";
    h += sizeNames[i];
    h += "</option>";
  }
  h += F("</select></label>"
         "<label>JPEG Quality (0=best)<input name='jpegQuality' type='number' min='0' max='63' value='");
  h += cfg.jpegQuality;
  h += F("'></label></div><div class='row'>"
         "<label>FPS<input name='fps' type='number' min='1' max='30' id='fpsin' value='");
  h += cfg.fps;
  h += F("'></label>"
         "<label>XCLK MHz<input name='xclkMhz' type='number' min='10' max='40' value='");
  h += cfg.xclkMhz;
  h += F("'></label></div>"
         "<label>Burst Frames<input name='burstFrames' type='number' min='1' max='900'"
         " id='bfin' value='");
  h += cfg.burstFrames;
  h += F("'> <span style='color:#555;font-size:.8em' id='bdur'></span></label>");

  // ---- Sensor Tuning (collapsible) ----
  h += F("<details id='sensorDetails'>"
         "<summary>Sensor Tuning</summary>"
         "<div class='sensor-grid'>");

  // Sliders — brightness, contrast, saturation, sharpness (each -2..2)
  // These go in the 2-col grid so two per row
  addSlider(h, "camBright",   "Brightness (-2..2)",  -2, 2, cfg.camBrightness);
  addSlider(h, "camContrast", "Contrast (-2..2)",    -2, 2, cfg.camContrast);
  addSlider(h, "camSat",      "Saturation (-2..2)",  -2, 2, cfg.camSaturation);
  addSlider(h, "camSharp",    "Sharpness (-2..2)",   -2, 2, cfg.camSharpness);
  addSlider(h, "camDenoise",  "Denoise (0..8)",       0, 8, cfg.camDenoise);

  // Exposure control
  h += F("<div class='sensor-full'>"
         "<label><input type='checkbox' name='camAec' id='camAec'");
  if (cfg.camAec) h += " checked";
  h += F(" onchange=\"document.getElementById('aecManual').style.display=this.checked?'none':'block'\">"
         " Auto Exposure (AEC)</label>"
         "<div class='manual-row' id='aecManual' style='display:");
  h += cfg.camAec ? "none" : "block";
  h += F("'>");
  addSlider(h, "camAecVal", "Manual Exposure (0..1200)", 0, 1200, cfg.camAecVal);
  h += F("</div></div>");

  // Gain control
  h += F("<div class='sensor-full'>"
         "<label><input type='checkbox' name='camGain' id='camGain'");
  if (cfg.camGain) h += " checked";
  h += F(" onchange=\"document.getElementById('gainManual').style.display=this.checked?'none':'block'\">"
         " Auto Gain (AGC)</label>"
         "<div class='manual-row' id='gainManual' style='display:");
  h += cfg.camGain ? "none" : "block";
  h += F("'>");
  addSlider(h, "camGainCtrl", "Manual Gain (0..30)", 0, 30, cfg.camGainCtrl);
  h += F("</div></div>");

  // White balance
  h += F("<div class='sensor-full'>"
         "<label><input type='checkbox' name='camAwb' id='camAwb'");
  if (cfg.camAwb) h += " checked";
  h += F("> Auto White Balance</label>"
         "<label><input type='checkbox' name='camAwbGain' id='camAwbGain'");
  if (cfg.camAwbGain) h += " checked";
  h += F("> AWB Gain</label>"
         "<label>WB Mode"
         "<select name='camWbMode'>"
         "<option value='0'");
  if (cfg.camWbMode==0) h += " selected";
  h += F(">Auto</option><option value='1'");
  if (cfg.camWbMode==1) h += " selected";
  h += F(">Sunny</option><option value='2'");
  if (cfg.camWbMode==2) h += " selected";
  h += F(">Cloudy</option><option value='3'");
  if (cfg.camWbMode==3) h += " selected";
  h += F(">Office</option><option value='4'");
  if (cfg.camWbMode==4) h += " selected";
  h += F(">Home</option></select></label></div>");

  // Flip / correction
  h += F("<label><input type='checkbox' name='camVflip'");
  if (cfg.camVflip) h += " checked";
  h += F("> Vertical Flip</label>"
         "<label><input type='checkbox' name='camHflip'");
  if (cfg.camHflip) h += " checked";
  h += F("> Horizontal Mirror</label>"
         "<label><input type='checkbox' name='camLenc'");
  if (cfg.camLenc) h += " checked";
  h += F("> Lens Correction</label>"
         "<label><input type='checkbox' name='camDcw'");
  if (cfg.camDcw) h += " checked";
  h += F("> Downsize EN (DCW)</label>");

  // Apply Now button — sends sensor fields to /sensorapply without full save
  h += F("<div class='sensor-full' style='margin-top:.5em'>"
         "<button type='button' class='sec' onclick='applyNow()'>&#9881; Apply Now (no save)</button>"
         "</div>");

  h += F("</div></details>"); // end sensor-grid + details
  h += F("</div>");            // end Camera card

  // Trigger / network
  h += F("<div class='card'><h2>Trigger</h2>"
         "<label>UDP Trigger Port<input name='trigPort' type='number' value='");
  h += cfg.triggerPort;
  h += F("'></label></div>");

  h += F("<button type='button' onclick='saveCfg()'>&#128190; Save Config</button>"
         "</form>");

  // RTSP info
  h += F("<div class='card' style='margin-top:1em'><h2>RTSP Stream</h2>"
         "<code id='rtspUrl'>rtsp://");
  h += WiFi.localIP().toString();
  h += F(":554/mjpeg/1</code><br>"
         "<span style='font-size:.8em;color:#555'>Add as Media Source in OBS with this URL. "
         "Check 'Use hardware decoding' OFF. Uncheck 'Restart when active'.</span></div>");

  // JS
  h += F("<script>"
         "function saveCfg(){"
         "  const f=document.getElementById('cfg');"
         "  const d=new URLSearchParams(new FormData(f));"
         "  fetch('/save',{method:'POST',body:d})"
         "  .then(r=>r.json()).then(j=>alert(j.ok?'Saved!':'Error'));"
         "}"
         "function applyNow(){"
         "  const f=document.getElementById('cfg');"
         "  const d=new URLSearchParams(new FormData(f));"
         "  fetch('/sensorapply',{method:'POST',body:d})"
         "  .then(r=>r.json()).then(j=>{"
         "    const btn=document.querySelector('button.sec[onclick=\'applyNow()\']');"
         "    if(btn){const orig=btn.textContent;btn.textContent=j.ok?'\u2713 Applied':'\u2717 Failed';"
         "    setTimeout(()=>btn.textContent=orig,2000);}"
         "  });"
         "}"
         "function updateStatus(){"
         "  fetch('/status').then(r=>r.json()).then(j=>{"
         "    document.getElementById('bstatus').textContent=j.state;"
         "    document.getElementById('rssi').textContent='RSSI '+j.rssi+'dBm | '+j.frames+' frames';"
         "  }).catch(()=>{});"
         "}"
         "function clipDur(){"
         "  const fr=+document.getElementById('bfin').value||0;"
         "  const fp=+document.getElementById('fpsin').value||15;"
         "  return fr/fp;"
         "}"
         "function updateDur(){"
         "  const d=clipDur();"
         "  document.getElementById('bdur').textContent='= '+d.toFixed(1)+'s';"
         "  updateVisHint();"
         "}"
         "function updateVisHint(){"
         "  const v=+document.getElementById('visSecs').value;"
         "  const hint=document.getElementById('visHint');"
         "  if(v===0){hint.textContent='auto: '+clipDur().toFixed(1)+'s';}"
         "  else{hint.textContent=v+'s manual';}"
         "}"
         // Restore sensor details open state across page reloads
         "(function(){"
         "  if(sessionStorage.getItem('sensorOpen')==='1')"
         "    document.getElementById('sensorDetails').open=true;"
         "  document.getElementById('sensorDetails').addEventListener('toggle',function(){"
         "    sessionStorage.setItem('sensorOpen',this.open?'1':'0');"
         "  });"
         "})();"
         "document.getElementById('bfin').addEventListener('input',updateDur);"
         "document.getElementById('fpsin').addEventListener('input',updateDur);"
         "document.getElementById('visSecs').addEventListener('input',updateVisHint);"
         "updateDur();updateVisHint();setInterval(updateStatus,2000);"
         "</script></body></html>");
  return h;
}
