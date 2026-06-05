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

// Emit a labeled range slider with live value readout.
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

// Emit a checkbox without a hidden fallback — the server handler defaults
// absent checkboxes to 0, so we don't need the duplicate-name trick.
static void addCheck(String& h, const char* name, const char* label, bool checked,
                     const char* onchange = nullptr) {
  h += "<label><input type='checkbox' name='";
  h += name;
  h += "' value='1'";
  if (checked) h += " checked";
  if (onchange) {
    h += " onchange=\"";
    h += onchange;
    h += "\"";
  }
  h += "> ";
  h += label;
  h += "</label>";
}

String buildConfigPage(bool camOk) {
  String h;
  h.reserve(9500);

  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>BurstCast</title>"
         "<style>"
         "body{font-family:monospace;background:#111;color:#ccc;margin:0;padding:1em}"
         "h1{color:#0f0;margin:0 0 .5em}h2{color:#0a0;font-size:.9em;margin:1em 0 .4em}"
         ".card{background:#1a1a1a;border:1px solid #333;border-radius:6px;padding:1em;margin-bottom:1em;max-width:600px}"
         "label{display:block;margin:.4em 0;font-size:.85em}"
         "input[type=text],input[type=number],input[type=password],select{"
         "background:#000;color:#0f0;border:1px solid #444;padding:.3em .5em;"
         "font-family:monospace;width:100%;box-sizing:border-box}"
         "input[type=range]{accent-color:#0f0;cursor:pointer;width:100%}"
         "input[type=checkbox]{accent-color:#0f0;width:auto;cursor:pointer}"
         "input[type=password]{letter-spacing:.1em}"
         ".row{display:grid;grid-template-columns:1fr 1fr;gap:.5em}"
         "button{background:#0f0;color:#000;border:none;padding:.5em 1.2em;cursor:pointer;font-weight:bold;margin-top:.5em}"
         "button.sec{background:#444;color:#ccc}"
         ".status{display:inline-block;padding:.2em .6em;border-radius:3px;font-size:.8em;font-weight:bold}"
         ".ok{background:#0a0;color:#0f0}.fail{background:#500;color:#f00}"
         ".hint{color:#555;font-size:.78em;margin-top:.15em}"
         "details{margin-top:.5em}"
         "details>summary{cursor:pointer;color:#0a0;font-size:.85em;user-select:none;padding:.3em 0;list-style:none}"
         "details>summary::-webkit-details-marker{display:none}"
         "details>summary::before{content:'\u25b6 ';}"
         "details[open]>summary::before{content:'\u25bc ';}"
         ".sg{display:grid;grid-template-columns:1fr 1fr;gap:.6em;margin-top:.6em}"
         ".sf{grid-column:1/-1}"
         ".mrow{margin:.3em 0;padding:.4em .6em;background:#111;border-left:2px solid #0a0;font-size:.82em}"
         "</style></head><body>");

  // Status bar
  h += F("<div class='card'>");
  h += "<span class='status ";
  h += camOk ? "ok'>Cam OK" : "fail'>Cam FAIL";
  h += "</span> <span id='bstatus' class='status sec'>Idle</span> ";
  h += "<span id='rssi' style='font-size:.8em;color:#555'></span></div>";

  // Preview
  h += F("<div class='card'><h2>Preview</h2>"
         "<img id='prev' src='/cam.jpg' style='width:100%;max-width:320px;border:1px solid #333'><br>"
         "<button class='sec' onclick=\"document.getElementById('prev').src='/cam.jpg?t='+Date.now()\">Refresh</button>"
         " <button class='sec' onclick=\"window.open('/stream','_blank')\">Live Stream</button></div>");

  // Trigger
  h += F("<div class='card'><h2>Trigger</h2>"
         "<button onclick=\"fetch('/trigger').then(r=>r.json()).then(j=>alert(JSON.stringify(j)))\">&#9654; Trigger Burst</button>"
         " <button class='sec' onclick=\"fetch('/clearbuffer').then(()=>alert('Buffer cleared'))\">Clear Buffer</button></div>");

  // ---- Begin form ----
  h += F("<form id='cfg'>");

  // OBS WebSocket
  h += F("<div class='card'><h2>OBS WebSocket</h2>"
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
         "<label>Source Name<input name='obsSourceName' value='");
  h += cfg.obsSourceName;
  h += F("'></label>"
         "<div class='row'>"
         "<label>Source Visible (sec)<input name='visibleSecs' type='number' min='0' max='3600'"
         " id='visSecs' value='");
  h += cfg.visibleSecs;
  h += F("'><div class='hint' id='visHint'></div></label>"
         "<div style='padding-top:1.4em;font-size:.8em;color:#555'>0 = auto (matches clip length)</div>"
         "</div></div>");

  // Camera
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
    if (i == (int)cfg.frameSize) h += "' selected>";
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
  h += F("'></label><label>XCLK MHz<input name='xclkMhz' type='number' min='10' max='40' value='");
  h += cfg.xclkMhz;
  h += F("'></label></div>"
         "<label>Burst Frames<input name='burstFrames' type='number' min='1' max='900' id='bfin' value='");
  h += cfg.burstFrames;
  h += F("'> <span style='color:#555;font-size:.8em' id='bdur'></span></label>");

  // ---- Sensor Tuning (collapsible) ----
  h += F("<details id='sdet'><summary>&#9881; Sensor Tuning</summary><div class='sg'>");

  addSlider(h, "camBright",   "Brightness (-2..2)",  -2,   2, cfg.camBrightness);
  addSlider(h, "camContrast", "Contrast (-2..2)",    -2,   2, cfg.camContrast);
  addSlider(h, "camSat",      "Saturation (-2..2)",  -2,   2, cfg.camSaturation);
  addSlider(h, "camSharp",    "Sharpness (-2..2)",   -2,   2, cfg.camSharpness);
  addSlider(h, "camDenoise",  "Denoise (0..8)",        0,   8, cfg.camDenoise);

  // AEC
  h += F("<div class='sf'>");
  addCheck(h, "camAec", "Auto Exposure (AEC)", cfg.camAec,
           "document.getElementById('aecM').style.display=this.checked?'none':'block'");
  h += "<div class='mrow' id='aecM' style='display:";
  h += cfg.camAec ? "none" : "block";
  h += "'>";
  addSlider(h, "camAecVal", "Manual Exposure (0..1200)", 0, 1200, cfg.camAecVal);
  h += F("</div></div>");

  // AGC
  h += F("<div class='sf'>");
  addCheck(h, "camGain", "Auto Gain (AGC)", cfg.camGain,
           "document.getElementById('gainM').style.display=this.checked?'none':'block'");
  h += "<div class='mrow' id='gainM' style='display:";
  h += cfg.camGain ? "none" : "block";
  h += "'>";
  addSlider(h, "camGainCtrl", "Manual Gain (0..30)", 0, 30, cfg.camGainCtrl);
  h += F("</div></div>");

  // AWB
  h += F("<div class='sf'>");
  addCheck(h, "camAwb",    "Auto White Balance", cfg.camAwb);
  addCheck(h, "camAwbGain","AWB Gain",           cfg.camAwbGain);
  h += F("<label>WB Mode<select name='camWbMode'>"
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

  // Flips / corrections
  h += F("<div>");
  addCheck(h, "camVflip", "Vertical Flip",     cfg.camVflip);
  addCheck(h, "camHflip", "Horizontal Mirror",  cfg.camHflip);
  h += F("</div><div>");
  addCheck(h, "camLenc", "Lens Correction",     cfg.camLenc);
  addCheck(h, "camDcw",  "Downsize EN (DCW)",   cfg.camDcw);
  h += F("</div>");

  // Apply Now
  h += F("<div class='sf' style='margin-top:.6em'>"
         "<button type='button' id='applyNowBtn' class='sec' onclick='applyNow()'>"
         "&#9881; Apply Now (live, no save)</button></div>");

  h += F("</div></details></div>"); // end sg + sdet + camera card

  // Trigger/network
  h += F("<div class='card'><h2>Trigger</h2>"
         "<label>UDP Trigger Port<input name='trigPort' type='number' value='");
  h += cfg.triggerPort;
  h += F("'></label></div>");

  h += F("<button type='button' onclick='saveCfg()'>&#128190; Save Config</button>");
  h += F("</form>"); // ---- end form ----

  // RTSP info
  h += F("<div class='card' style='margin-top:1em'><h2>RTSP Stream</h2><code>rtsp://");
  h += WiFi.localIP().toString();
  h += F(":554/mjpeg/1</code><br>"
         "<span style='font-size:.8em;color:#555'>Add as Media Source in OBS. "
         "Disable hardware decoding. Set network buffering to 0.</span></div>");

  // JS
  h += F("<script>"
  // saveCfg: serialize entire form and POST to /save
  "function saveCfg(){"
  "  var d=new URLSearchParams(new FormData(document.getElementById('cfg')));"
  "  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d.toString()})"
  "  .then(function(r){return r.json();})"
  "  .then(function(j){alert(j.ok?'Saved!':'Save failed');});"
  "}"
  // applyNow: same serialization, POST to /sensorapply, then refresh preview
  "function applyNow(){"
  "  var btn=document.getElementById('applyNowBtn');"
  "  var d=new URLSearchParams(new FormData(document.getElementById('cfg')));"
  "  fetch('/sensorapply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d.toString()})"
  "  .then(function(r){return r.json();})"
  "  .then(function(j){"
  "    btn.textContent=j.ok?'\u2713 Applied!':'\u2717 Failed';"
  "    setTimeout(function(){"
  "      btn.textContent='\u2699 Apply Now (live, no save)';"
  "      if(j.ok)document.getElementById('prev').src='/cam.jpg?t='+Date.now();"
  "    },800);"
  "  });"
  "}"
  "function updateStatus(){"
  "  fetch('/status').then(function(r){return r.json();}).then(function(j){"
  "    document.getElementById('bstatus').textContent=j.state;"
  "    document.getElementById('rssi').textContent='RSSI '+j.rssi+'dBm | '+j.frames+' frames';"
  "  }).catch(function(){});"
  "}"
  "function clipDur(){"
  "  return(+document.getElementById('bfin').value||0)/(+document.getElementById('fpsin').value||15);"
  "}"
  "function updateDur(){"
  "  document.getElementById('bdur').textContent='= '+clipDur().toFixed(1)+'s';"
  "  updateVisHint();"
  "}"
  "function updateVisHint(){"
  "  var v=+document.getElementById('visSecs').value;"
  "  document.getElementById('visHint').textContent=v===0?'auto: '+clipDur().toFixed(1)+'s':v+'s manual';"
  "}"
  // remember sensor panel open/closed state
  "(function(){"
  "  var d=document.getElementById('sdet');"
  "  if(sessionStorage.getItem('sdet')==='1')d.open=true;"
  "  d.addEventListener('toggle',function(){sessionStorage.setItem('sdet',d.open?'1':'0');});"
  "})();"
  "document.getElementById('bfin').addEventListener('input',updateDur);"
  "document.getElementById('fpsin').addEventListener('input',updateDur);"
  "document.getElementById('visSecs').addEventListener('input',updateVisHint);"
  "updateDur();updateVisHint();setInterval(updateStatus,2000);"
  "</script></body></html>");
  return h;
}
