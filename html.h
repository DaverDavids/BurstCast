#pragma once
// ============================================================
//  html.h — BurstCast web UI
//  NOTE: config.h must be included before this file.
//        cfg extern is declared there.
// ============================================================

#include "config.h"

static const char* const FRAME_SIZE_LABELS[] = {
  "QQVGA (160x120)",   // 0
  "QCIF (176x144)",    // 1
  "HQVGA (240x176)",   // 2
  "240x240",           // 3
  "QVGA (320x240)",    // 4
  "CIF (400x296)",     // 5
  "HVGA (480x320)",    // 6
  "VGA (640x480)",     // 7  <- default
  "SVGA (800x600)",    // 8
  "XGA (1024x768)",    // 9
  "HD (1280x720)",     // 10
  "SXGA (1280x1024)",  // 11
  "UXGA (1600x1200)"   // 12
};
static const int FRAME_SIZE_COUNT = 13;

// ---- Captive portal WiFi credential form (stored in flash) ----
static const char CAPTIVE_FORM[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BurstCast Setup</title>
<style>
body{font-family:monospace;background:#111;color:#eee;max-width:440px;margin:40px auto;padding:0 16px}
h1{color:#f90;margin-bottom:2px}p{color:#aaa;font-size:.85em;margin-top:0}
label{display:block;margin-top:12px;font-size:.9em;color:#ccc}
input{width:100%;padding:8px;background:#222;border:1px solid #444;color:#eee;
      border-radius:4px;box-sizing:border-box;margin-top:4px}
button{margin-top:20px;width:100%;padding:10px;background:#f90;border:none;
       border-radius:4px;font-size:1em;cursor:pointer;color:#111;font-weight:bold}
button:hover{background:#fb3}
</style></head><body>
<h1>&#x1F4F7; BurstCast</h1>
<p>Enter WiFi credentials to connect.</p>
<form method="POST" action="/save">
  <label>SSID<input name="ssid" type="text" autocomplete="off" placeholder="Network name"></label>
  <label>Password<input name="psk" type="password" autocomplete="off"></label>
  <button type="submit">Connect &amp; Save</button>
</form>
</body></html>
)html";

// ---- Main config page ----
String buildConfigPage() {
  String h;
  h.reserve(3000);

  h += F("<!DOCTYPE html><html><head>"
         "<meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>BurstCast Config</title>"
         "<style>"
         "body{font-family:monospace;background:#111;color:#eee;max-width:520px;margin:40px auto;padding:0 16px}"
         "h1{color:#f90;margin-bottom:2px}"
         "h2{color:#aaa;font-size:.9em;margin:18px 0 5px;border-bottom:1px solid #2a2a2a;padding-bottom:3px}"
         "label{display:block;margin-top:10px;font-size:.88em;color:#ccc}"
         "input,select{width:100%;padding:7px 8px;background:#1a1a1a;border:1px solid #3a3a3a;"
         "color:#eee;border-radius:4px;box-sizing:border-box;margin-top:3px}"
         ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
         "button{margin-top:16px;width:100%;padding:10px;border:none;border-radius:4px;"
         "font-size:1em;cursor:pointer;font-weight:bold}"
         ".btn-save{background:#f90;color:#111}.btn-save:hover{background:#fb3}"
         ".btn-trig{background:#39c;color:#fff;margin-top:8px}.btn-trig:hover{background:#4ad}"
         ".status{background:#1a1a1a;border:1px solid #2a2a2a;border-radius:4px;"
         "padding:9px 12px;margin-top:14px;font-size:.82em;color:#999}"
         ".on{color:#4f4}.off{color:#f44}"
         "</style></head><body>"
         "<h1>&#x1F4F7; BurstCast</h1>");

  h += "<div class='status' id='stat'>Fetching status...</div>";

  h += "<form method='POST' action='/save'>";

  h += "<h2>OBS / Stream Target</h2>";
  h += "<div class='row'>";
  h += "<label>OBS IP<input name='obsIp' value='";
  h += cfg.obsIp;
  h += "' placeholder='192.168.x.x'></label>";
  h += "<label>RTP Port<input name='obsPort' type='number' min='1' max='65535' value='";
  h += String(cfg.obsPort);
  h += "'></label></div>";

  h += "<h2>Trigger</h2>";
  h += "<label>UDP Trigger Port<input name='trigPort' type='number' min='1' max='65535' value='";
  h += String(cfg.triggerPort);
  h += "'></label>";

  h += "<h2>Burst</h2>";
  h += "<div class='row'>";
  h += "<label>Frames<input name='burstFrames' type='number' min='1' max='9999' value='";
  h += String(cfg.burstFrames);
  h += "'></label>";
  h += "<label>JPEG Quality (0=best)<input name='jpegQuality' type='number' min='0' max='63' value='";
  h += String(cfg.jpegQuality);
  h += "'></label></div>";

  h += "<label>Frame Size<select name='frameSize'>";
  for (int i = 0; i < FRAME_SIZE_COUNT; i++) {
    h += "<option value='";
    h += String(i);
    h += (i == cfg.frameSize) ? "' selected>" : "'>";
    h += FRAME_SIZE_LABELS[i];
    h += "</option>";
  }
  h += "</select></label>";

  h += "<button type='submit' class='btn-save'>&#x1F4BE; Save Settings</button>";
  h += "</form>";

  h += "<form method='GET' action='/trigger'>";
  h += "<button type='submit' class='btn-trig'>&#x25B6; Manual Trigger</button>";
  h += "</form>";

  h += R"js(
<script>
function poll(){
  fetch('/status').then(function(r){return r.json();}).then(function(d){
    var s=document.getElementById('stat');
    s.innerHTML='IP: <b>'+d.ip+'</b> &nbsp; RSSI: <b>'+d.rssi+' dBm</b>'
      +' &nbsp; Burst: <b class="'+(d.burst?'on':'off')+'">'+(d.burst?'ACTIVE':'IDLE')+'</b>'
      +' &nbsp; Frames: <b>'+d.framesSent+'</b>';
  }).catch(function(){});
}
poll();
setInterval(poll,2000);
</script>
)js";

  h += "</body></html>";
  return h;
}
