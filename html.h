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

String buildConfigPage(bool camOk) {
  uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
  static const uint16_t FW[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
  static const uint16_t FH[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

  String h;
  h.reserve(6500);
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>BurstCast</title>"
         "<style>"
         "body{font-family:monospace;background:#111;color:#ccc;margin:0;padding:1em}"
         "h1{color:#0f0;margin:0 0 .5em}h2{color:#0a0;font-size:.9em;margin:1em 0 .4em}"
         ".card{background:#1a1a1a;border:1px solid #333;border-radius:6px;padding:1em;margin-bottom:1em;max-width:600px}"
         "label{display:block;margin:.4em 0;font-size:.85em}"
         "input,select{background:#000;color:#0f0;border:1px solid #444;padding:.3em .5em;font-family:monospace;width:100%;box-sizing:border-box}"
         "input[type=password]{letter-spacing:.1em}"
         ".row{display:grid;grid-template-columns:1fr 1fr;gap:.5em}"
         "button{background:#0f0;color:#000;border:none;padding:.5em 1.2em;cursor:pointer;font-weight:bold;margin-top:.5em}"
         "button.sec{background:#444;color:#ccc}"
         "button.danger{background:#f00;color:#fff}"
         ".status{display:inline-block;padding:.2em .6em;border-radius:3px;font-size:.8em;font-weight:bold}"
         ".ok{background:#0a0;color:#0f0}.fail{background:#500;color:#f00}"
         ".hint{color:#555;font-size:.78em;margin-top:.15em}"
         "#log{background:#000;border:1px solid #333;padding:.5em;height:80px;overflow-y:auto;font-size:.75em;color:#888}"
         "</style></head><body>");

  h += F("<h1>&#127909; BurstCast</h1>");

  // Status bar
  h += F("<div class='card'>");
  h += "<span class='status "; h += camOk ? "ok'>Cam OK" : "fail'>Cam FAIL"; h += "</span> ";
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
  h += F("'>"
         "<div class='hint' id='visHint'></div></label>"
         "<div style='padding-top:1.4em;font-size:.8em;color:#555'>"
         "0 = auto (matches clip length)</div>"
         "</div>"
         "</div>");

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
    h += "'";
    if (i == cfg.frameSize) h += " selected";
    h += ">";
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
  h += F("'> <span style='color:#555;font-size:.8em' id='bdur'></span></label></div>");

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
         "  .then(r=>r.json()).then(j=>alert(j.ok?'Saved! Reconnecting...':'Error'));"
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
         "  if(v===0){"
         "    hint.textContent='auto: '+clipDur().toFixed(1)+'s';"
         "  } else {"
         "    hint.textContent=v+'s manual';"
         "  }"
         "}"
         "document.getElementById('bfin').addEventListener('input',updateDur);"
         "document.getElementById('fpsin').addEventListener('input',updateDur);"
         "document.getElementById('visSecs').addEventListener('input',updateVisHint);"
         "updateDur(); updateVisHint(); setInterval(updateStatus,2000);"
         "</script></body></html>");
  return h;
}
