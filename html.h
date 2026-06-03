#pragma once
// ============================================================
//  html.h — BurstCast web UI
//  All HTML strings live here; included by BurstCast.ino
// ============================================================

// Forward-declare config struct fields used in the builder
// (actual struct is in BurstCast.ino)
extern struct Config cfg;

// Frame size option labels matching esp_camera framesize_t enum values
// that are practical for JPEG streaming on ESP32-S3
static const char* FRAME_SIZE_LABELS[] = {
  "QQVGA (160x120)",  // 0
  "QCIF (176x144)",   // 1
  "HQVGA (240x176)",  // 2
  "240x240",          // 3
  "QVGA (320x240)",   // 4
  "CIF (400x296)",    // 5
  "HVGA (480x320)",   // 6
  "VGA (640x480)",    // 7 ← default
  "SVGA (800x600)",   // 8
  "XGA (1024x768)",   // 9
  "HD (1280x720)",    // 10
  "SXGA (1280x1024)", // 11
  "UXGA (1600x1200)"  // 12
};
static const int FRAME_SIZE_COUNT = 13;

// ---- Captive portal WiFi form ----
static const char CAPTIVE_FORM[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BurstCast Setup</title>
<style>
  body{font-family:monospace;background:#111;color:#eee;max-width:480px;margin:40px auto;padding:0 16px}
  h1{color:#f90;margin-bottom:4px}p{color:#aaa;font-size:.85em;margin-top:0}
  label{display:block;margin-top:12px;font-size:.9em;color:#ccc}
  input,select{width:100%;padding:8px;background:#222;border:1px solid #444;color:#eee;
               border-radius:4px;box-sizing:border-box;margin-top:4px}
  button{margin-top:20px;width:100%;padding:10px;background:#f90;border:none;
         border-radius:4px;font-size:1em;cursor:pointer;color:#111;font-weight:bold}
  button:hover{background:#fb3}
</style></head><body>
<h1>&#x1F4F7; BurstCast</h1>
<p>Connect to your WiFi network to continue.</p>
<form method="POST" action="/save">
  <label>WiFi SSID
    <input name="ssid" type="text" placeholder="Your network name" autocomplete="off">
  </label>
  <label>WiFi Password
    <input name="psk" type="password" placeholder="Password" autocomplete="off">
  </label>
  <button type="submit">Connect &amp; Save</button>
</form>
</body></html>
)rawhtml";

// ---- Main config page builder ----
String buildConfigPage() {
  String h = F(
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BurstCast Config</title>"
    "<style>"
    "body{font-family:monospace;background:#111;color:#eee;max-width:520px;margin:40px auto;padding:0 16px}"
    "h1{color:#f90;margin-bottom:2px}h2{color:#aaa;font-size:.95em;margin:20px 0 6px;border-bottom:1px solid #333;padding-bottom:4px}"
    "label{display:block;margin-top:10px;font-size:.88em;color:#ccc}"
    "input,select{width:100%;padding:7px 8px;background:#1a1a1a;border:1px solid #444;color:#eee;"
    "border-radius:4px;box-sizing:border-box;margin-top:3px}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
    "button{margin-top:20px;width:100%;padding:10px;background:#f90;border:none;"
    "border-radius:4px;font-size:1em;cursor:pointer;color:#111;font-weight:bold}"
    "button:hover{background:#fb3}"
    ".status{background:#1a1a1a;border:1px solid #333;border-radius:4px;padding:10px;margin-top:16px;font-size:.82em;color:#aaa}"
    ".badge{display:inline-block;padding:2px 8px;border-radius:3px;font-size:.8em}"
    ".on{background:#1a4a1a;color:#4f4}"
    ".off{background:#3a1a1a;color:#f44}"
    "</style></head><body>"
    "<h1>&#x1F4F7; BurstCast</h1>"
  );

  // Live status bar
  h += "<div class='status' id='stat'>Loading status...</div>";

  h += "<form method='POST' action='/save'>";

  // --- OBS Stream ---
  h += "<h2>OBS / Stream Target</h2>";
  h += "<div class='row'>";
  h += "<label>OBS IP Address<input name='obsIp' value='" + String(cfg.obsIp) + "' placeholder='192.168.1.x'></label>";
  h += "<label>OBS RTSP Port<input name='obsPort' type='number' value='" + String(cfg.obsPort) + "' min='1' max='65535'></label>";
  h += "</div>";

  // --- Trigger ---
  h += "<h2>Trigger</h2>";
  h += "<label>UDP Trigger Port<input name='trigPort' type='number' value='" + String(cfg.triggerPort) + "' min='1' max='65535'></label>";

  // --- Burst ---
  h += "<h2>Burst Settings</h2>";
  h += "<div class='row'>";
  h += "<label>Burst Frames<input name='burstFrames' type='number' value='" + String(cfg.burstFrames) + "' min='1' max='1000'></label>";
  h += "<label>JPEG Quality (0=best)<input name='jpegQuality' type='number' value='" + String(cfg.jpegQuality) + "' min='0' max='63'></label>";
  h += "</div>";

  // Frame size dropdown
  h += "<label>Frame Size<select name='frameSize'>";
  for (int i = 0; i < FRAME_SIZE_COUNT; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == cfg.frameSize) h += " selected";
    h += ">";
    h += FRAME_SIZE_LABELS[i];
    h += "</option>";
  }
  h += "</select></label>";

  h += "<button type='submit'>&#x1F4BE; Save Settings</button>";
  h += "</form>";

  // Manual trigger button (separate form / GET)
  h += "<form method='GET' action='/trigger' style='margin-top:8px'>";
  h += "<button type='submit' style='background:#39c;color:#fff'>&#x25B6; Manual Trigger</button>";
  h += "</form>";

  // JS status poller
  h += R"rawjs(
<script>
function poll(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('stat').innerHTML=
      'IP: <b>'+d.ip+'</b> &nbsp; RSSI: <b>'+d.rssi+'dBm</b> &nbsp; '
      +'Burst: <span class="badge '+(d.burst?'on':'off')+'">'+(d.burst?'ACTIVE':'IDLE')+'</span>'
      +' &nbsp; Frames sent: <b>'+d.framesSent+'</b>';
  }).catch(()=>{});
}
poll();
setInterval(poll,2000);
</script>
)rawjs";

  h += "</body></html>";
  return h;
}
