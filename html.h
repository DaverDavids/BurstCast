#pragma once
#include "config.h"
#include <WiFi.h>

static const char* const FRAME_SIZE_LABELS[] = {
  "QQVGA (160x120)","QCIF (176x144)","HQVGA (240x176)","240x240",
  "QVGA (320x240)","CIF (400x296)","HVGA (480x320)","VGA (640x480)",
  "SVGA (800x600)","XGA (1024x768)","HD (1280x720)","SXGA (1280x1024)","UXGA (1600x1200)"
};
static const int FRAME_SIZE_COUNT = 13;

static const char CAPTIVE_FORM[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BurstCast Setup</title>
<style>body{font-family:monospace;background:#111;color:#eee;max-width:440px;margin:40px auto;padding:0 16px}
h1{color:#f90}label{display:block;margin-top:12px;font-size:.9em;color:#ccc}
input{width:100%;padding:8px;background:#222;border:1px solid #444;color:#eee;border-radius:4px;box-sizing:border-box;margin-top:4px}
button{margin-top:20px;width:100%;padding:10px;background:#f90;border:none;border-radius:4px;font-size:1em;cursor:pointer;color:#111;font-weight:bold}</style></head><body>
<h1>&#x1F4F7; BurstCast</h1>
<form method="POST" action="/save">
  <label>SSID<input name="ssid" type="text" autocomplete="off"></label>
  <label>Password<input name="psk" type="password" autocomplete="off"></label>
  <button type="submit">Connect &amp; Save</button>
</form></body></html>
)html";

String buildConfigPage(bool camOk) {
  String ip = WiFi.localIP().toString();
  String h;
  h.reserve(5500);

  h += F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BurstCast</title><style>"
    "body{font-family:monospace;background:#111;color:#eee;max-width:560px;margin:0 auto;padding:16px 16px 48px}"
    "h1{color:#f90;margin-bottom:2px}"
    "h2{color:#888;font-size:.88em;margin:18px 0 4px;border-bottom:1px solid #222;padding-bottom:3px}"
    "label{display:block;margin-top:10px;font-size:.87em;color:#bbb}"
    "input,select{width:100%;padding:7px 8px;background:#1a1a1a;border:1px solid #333;color:#eee;"
      "border-radius:4px;box-sizing:border-box;margin-top:3px}"
    ".row2{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
    ".row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}"
    ".btn{margin-top:10px;width:100%;padding:10px;border:none;border-radius:4px;"
      "font-size:1em;cursor:pointer;font-weight:bold;transition:opacity .15s}"
    ".btn:active{opacity:.6}"
    ".btn-save{background:#f90;color:#111}"
    ".btn-trig{background:#39c;color:#fff}"
    ".btn-sdp{background:#292;color:#fff}"
    ".stat{background:#161616;border:1px solid #222;border-radius:4px;padding:8px 12px;"
      "margin-top:12px;font-size:.8em;color:#888}"
    ".cam-wrap{position:relative;margin-top:10px;background:#0a0a0a;border:1px solid #222;"
      "border-radius:6px;overflow:hidden;text-align:center;min-height:80px}"
    ".cam-wrap img{width:100%;height:auto;display:block}"
    ".cam-err{padding:24px;color:#f44;font-size:.85em}"
    ".cam-badge{position:absolute;top:6px;right:8px;font-size:.72em;padding:2px 7px;"
      "border-radius:10px;background:rgba(0,0,0,.65)}"
    ".on{color:#4f4}.off{color:#f44}"
    ".note{font-size:.75em;color:#555;margin-top:3px}"
    ".toast{display:none;position:fixed;bottom:18px;left:50%;transform:translateX(-50%);"
      "background:#222;color:#eee;padding:7px 18px;border-radius:6px;font-size:.82em;"
      "z-index:99;border:1px solid #444}"
    "</style></head><body>"
    "<h1>&#x1F4F7; BurstCast</h1>");

  h += "<div class='stat' id='stat'>Loading...</div>";
  h += "<div class='toast' id='toast'></div>";

  // Camera preview
  h += "<h2>Camera Preview</h2>";
  h += "<div class='cam-wrap' id='camwrap'>";
  if (camOk) {
    h += "<img id='camimg' src='/cam.jpg' alt='camera preview'>";
    h += "<span class='cam-badge on' id='cambadge'>&#x25CF; LIVE</span>";
  } else {
    h += "<div class='cam-err'>&#x26A0; Camera init failed &mdash; check pin map &amp; reboot</div>";
    h += "<span class='cam-badge off'>&#x25CF; NO CAM</span>";
  }
  h += "</div>";
  if (camOk) {
    h += "<p class='note'>Snapshot refreshes every 2s &nbsp;|&nbsp; "
         "<a href='/stream' target='_blank' style='color:#f90'>Open live MJPEG stream &rarr;</a></p>";
  }

  // Config form
  h += "<form id='cfg'>";

  h += "<h2>OBS / Stream Target</h2>";
  h += "<div class='row2'>";
  h += "<label>OBS IP<input name='obsIp' value='" + String(cfg.obsIp) + "' placeholder='192.168.x.x'></label>";
  h += "<label>RTP Port<input name='obsPort' type='number' min='1' max='65535' value='" + String(cfg.obsPort) + "'></label>";
  h += "</div>";
  h += "<p class='note'>SDP: <a href='/stream.sdp' style='color:#f90'>http://" + ip + "/stream.sdp</a></p>";

  h += "<h2>Trigger</h2>";
  h += "<label>UDP Trigger Port<input name='trigPort' type='number' min='1' max='65535' value='" + String(cfg.triggerPort) + "'></label>";

  h += "<h2>Camera</h2>";
  h += "<label>Frame Size<select name='frameSize'>";
  for (int i = 0; i < FRAME_SIZE_COUNT; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == cfg.frameSize) h += " selected";
    h += ">" + String(FRAME_SIZE_LABELS[i]) + "</option>";
  }
  h += "</select></label>";
  h += "<div class='row3'>";
  h += "<label>FPS<input name='fps' type='number' min='1' max='30' value='" + String(cfg.fps) + "'></label>";
  h += "<label>XCLK MHz<select name='xclkMhz'>";
  const int xclk[] = {8,16,20,24};
  for (int i = 0; i < 4; i++) {
    h += "<option value='" + String(xclk[i]) + "'";
    if (xclk[i] == cfg.xclkMhz) h += " selected";
    h += ">" + String(xclk[i]) + "</option>";
  }
  h += "</select></label>";
  h += "<label>JPEG Q<input name='jpegQuality' type='number' min='0' max='63' value='" + String(cfg.jpegQuality) + "'></label>";
  h += "</div>";
  h += "<p class='note'>XCLK &amp; frame size apply after reboot.</p>";

  h += "<h2>Burst</h2>";
  h += "<label>Frames per burst<input name='burstFrames' id='bf' type='number' min='1' max='9999' value='" + String(cfg.burstFrames) + "'></label>";
  h += "<p class='note' id='dur'>" + String((float)cfg.burstFrames / (cfg.fps > 0 ? cfg.fps : 1), 1) + "s at current FPS</p>";

  h += "<button type='submit' class='btn btn-save'>&#x1F4BE; Save Settings</button>";
  h += "</form>";
  h += "<button class='btn btn-trig' onclick='triggerBurst()'>&#x25B6; Manual Trigger</button>";
  h += "<button class='btn btn-sdp' onclick='location.href=\"/stream.sdp\"'>&#x1F4E5; Download SDP for OBS</button>";

  h += R"js(
<script>
function toast(msg,ok){
  var t=document.getElementById('toast');
  t.textContent=msg;t.style.background=ok?'#1a3a1a':'#3a1a1a';
  t.style.display='block';clearTimeout(t._t);
  t._t=setTimeout(function(){t.style.display='none';},2800);
}
document.getElementById('cfg').addEventListener('submit',function(e){
  e.preventDefault();
  fetch('/save',{method:'POST',body:new FormData(this)})
    .then(function(r){return r.json();})
    .then(function(d){toast(d.ok?'\u2705 Saved':'\u274C Error',d.ok);})
    .catch(function(){toast('\u274C No response',false);});
});
function triggerBurst(){
  fetch('/trigger')
    .then(function(r){return r.json();})
    .then(function(d){
      if(!d.camOk){toast('\u26A0 Camera not ready \u2014 check serial log',false);return;}
      toast('\u25B6 Burst triggered',true);
    }).catch(function(){toast('\u274C No response',false);});
}
function poll(){
  fetch('/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('stat').innerHTML=
      'IP: <b>'+d.ip+'</b> &nbsp; RSSI: <b>'+d.rssi+' dBm</b>'
      +' &nbsp; Burst: <b class="'+(d.burst?'on':'off')+'">'+(d.burst?'ACTIVE':'IDLE')+'</b>'
      +' &nbsp; Frames: <b>'+d.framesSent+'</b>'
      +' &nbsp; Cam: <b class="'+(d.camOk?'on':'off')+'">'+(d.camOk?'OK':'FAIL')+'</b>';
  }).catch(function(){});
}
var camImg=document.getElementById('camimg');
function refreshCam(){
  if(!camImg)return;
  var s=new Image();
  s.onload=function(){camImg.src=s.src;};
  s.src='/cam.jpg?t='+Date.now();
}
poll();setInterval(poll,2000);
if(camImg)setInterval(refreshCam,2000);
document.getElementById('bf').addEventListener('input',updateDur);
document.querySelector('[name=fps]').addEventListener('input',updateDur);
function updateDur(){
  var f=parseInt(document.getElementById('bf').value)||1;
  var fps=parseInt(document.querySelector('[name=fps]').value)||1;
  document.getElementById('dur').textContent=(f/fps).toFixed(1)+'s at current FPS';
}
</script>
)js";

  h += "</body></html>";
  return h;
}
