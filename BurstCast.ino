// ============================================================
//  BurstCast — ESP32-S3 WiFi-triggered burst cam
//  RTSP loop server + OBS WebSocket show/hide control
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <Secrets.h>
#include "config.h"
#include "camera.h"
#include "rtsp.h"
#include "obsws.h"
#include "html.h"

// Bundle Micro-RTSP sources directly so Arduino compiles them
#include "Micro-RTSP/src/CStreamer.cpp"
#include "Micro-RTSP/src/CRtspSession.cpp"

Preferences prefs;
WebServer   webServer(80);
DNSServer   dnsServer;
WiFiUDP     udpTrigger;

bool captivePortalMode = false;

static uint32_t recordStartMs  = 0;
static uint32_t recordInterval = 0;
static uint32_t showUntilMs    = 0;
static bool     obsHideArmed   = false;

// ============================================================
//  Preferences
// ============================================================
void loadConfig() {
  prefs.begin("burstcast", true);
  strlcpy(cfg.obsWsIp,       prefs.getString("obsWsIp",   "").c_str(), sizeof(cfg.obsWsIp));
  cfg.obsWsPort =             prefs.getUShort("obsWsPort", DEFAULT_OBS_WS_PORT);
  strlcpy(cfg.obsWsPass,     prefs.getString("obsWsPass", "").c_str(), sizeof(cfg.obsWsPass));
  strlcpy(cfg.obsSceneName,  prefs.getString("obsScene",  "").c_str(), sizeof(cfg.obsSceneName));
  strlcpy(cfg.obsSourceName, prefs.getString("obsSrc",    DEFAULT_SOURCE_NAME).c_str(), sizeof(cfg.obsSourceName));
  cfg.triggerPort  = prefs.getUShort("trigPort",     TRIGGER_PORT);
  cfg.burstFrames  = prefs.getUShort("burstFrames",  DEFAULT_BURST_FRAMES);
  cfg.jpegQuality  = prefs.getUChar ("jpegQuality",  DEFAULT_JPEG_QUALITY);
  cfg.frameSize    = prefs.getUChar ("frameSize",    DEFAULT_FRAME_SIZE);
  cfg.fps          = prefs.getUChar ("fps",          DEFAULT_FPS);
  cfg.xclkMhz      = prefs.getUChar ("xclkMhz",     DEFAULT_XCLK_MHZ);
  cfg.visibleSecs  = prefs.getUShort("visibleSecs",  DEFAULT_VISIBLE_SECS);
  prefs.end();
}

void saveConfig() {
  prefs.begin("burstcast", false);
  prefs.putString("obsWsIp",    cfg.obsWsIp);
  prefs.putUShort("obsWsPort",  cfg.obsWsPort);
  prefs.putString("obsWsPass",  cfg.obsWsPass);
  prefs.putString("obsScene",   cfg.obsSceneName);
  prefs.putString("obsSrc",     cfg.obsSourceName);
  prefs.putUShort("trigPort",   cfg.triggerPort);
  prefs.putUShort("burstFrames",cfg.burstFrames);
  prefs.putUChar ("jpegQuality",cfg.jpegQuality);
  prefs.putUChar ("frameSize",  cfg.frameSize);
  prefs.putUChar ("fps",        cfg.fps);
  prefs.putUChar ("xclkMhz",    cfg.xclkMhz);
  prefs.putUShort("visibleSecs",cfg.visibleSecs);
  prefs.end();
  Serial.println("[Config] Saved");
}

// ============================================================
//  Burst state machine
// ============================================================
void startBurst() {
  if (burstState == STATE_RECORDING) return;
  if (!cameraOk()) { Serial.println("[Burst] Camera not ready"); return; }
  Serial.println("[Burst] Recording started");
  bufferClear();
  burstState      = STATE_RECORDING;
  recordStartMs   = millis();
  recordInterval  = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
}

void handleRecording() {
  if (burstState != STATE_RECORDING) return;
  static uint32_t lastCapMs = 0;
  if (millis() - lastCapMs < recordInterval) return;
  lastCapMs = millis();

  if (bufferFrameCount() >= cfg.burstFrames) {
    burstState = STATE_LOOPING;
    float clipDur = (float)cfg.burstFrames / max((uint8_t)1, cfg.fps);
    Serial.printf("[Burst] Done — %u frames, %.1fs\n",
      (unsigned)bufferFrameCount(), clipDur);
    if (obsWsReady()) {
      obsSetSourceVisible(true);
      uint32_t displayMs = cfg.visibleSecs > 0
        ? (uint32_t)cfg.visibleSecs * 1000
        : (uint32_t)(clipDur * 1000);
      showUntilMs  = millis() + displayMs;
      obsHideArmed = true;
      Serial.printf("[OBS] Source visible for %.1fs\n", displayMs / 1000.0f);
    }
    return;
  }
  bufferAppendFrame();
}

void handleObsHide() {
  if (!obsHideArmed) return;
  if (millis() >= showUntilMs) {
    obsSetSourceVisible(false);
    obsHideArmed = false;
  }
}

// ============================================================
//  UDP trigger
// ============================================================
void handleTrigger() {
  int pktSize = udpTrigger.parsePacket();
  if (pktSize <= 0) return;
  char buf[32];
  int n = min(pktSize, (int)(sizeof(buf)-1));
  udpTrigger.read(buf, n); buf[n] = '\0';
  Serial.printf("[Trigger] UDP: %s\n", buf);
  startBurst();
  udpTrigger.beginPacket(udpTrigger.remoteIP(), udpTrigger.remotePort());
  udpTrigger.write((const uint8_t*)ACK_MSG, strlen(ACK_MSG));
  udpTrigger.endPacket();
}

// ============================================================
//  WiFi
// ============================================================
void startCaptivePortal() {
  captivePortalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[WiFi] Captive portal: %s\n", WiFi.softAPIP().toString().c_str());
}

void connectWiFi() {
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSIDIOT, MYPSKIOT);
  Serial.printf("[WiFi] Connecting to %s", MYSSIDIOT);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(250); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setTxPower(WIFI_TX_POWER);
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Failed — captive portal");
    startCaptivePortal();
  }
}

// ============================================================
//  Web server
// ============================================================
void setupWebServer() {
  webServer.onNotFound([]() {
    if (captivePortalMode) {
      webServer.sendHeader("Location", "http://192.168.4.1/");
      webServer.send(302, "text/plain", "");
    } else {
      webServer.send(404, "text/plain", "Not found");
    }
  });

  webServer.on("/", HTTP_GET, []() {
    webServer.send(200, "text/html",
      captivePortalMode ? FPSTR(CAPTIVE_FORM) : buildConfigPage(cameraOk()));
  });

  webServer.on("/save", HTTP_POST, []() {
    // Snapshot current camera-affecting values to detect changes
    uint8_t oldFrameSize = cfg.frameSize;
    uint8_t oldXclkMhz  = cfg.xclkMhz;

    if (webServer.hasArg("obsWsIp"))       strlcpy(cfg.obsWsIp,       webServer.arg("obsWsIp").c_str(),       sizeof(cfg.obsWsIp));
    if (webServer.hasArg("obsWsPort"))     cfg.obsWsPort     = webServer.arg("obsWsPort").toInt();
    if (webServer.hasArg("obsWsPass"))     strlcpy(cfg.obsWsPass,     webServer.arg("obsWsPass").c_str(),     sizeof(cfg.obsWsPass));
    if (webServer.hasArg("obsSceneName"))  strlcpy(cfg.obsSceneName,  webServer.arg("obsSceneName").c_str(),  sizeof(cfg.obsSceneName));
    if (webServer.hasArg("obsSourceName")) strlcpy(cfg.obsSourceName, webServer.arg("obsSourceName").c_str(), sizeof(cfg.obsSourceName));
    if (webServer.hasArg("trigPort"))      cfg.triggerPort   = webServer.arg("trigPort").toInt();
    if (webServer.hasArg("burstFrames"))   cfg.burstFrames   = webServer.arg("burstFrames").toInt();
    if (webServer.hasArg("jpegQuality"))   cfg.jpegQuality   = webServer.arg("jpegQuality").toInt();
    if (webServer.hasArg("frameSize"))     cfg.frameSize     = webServer.arg("frameSize").toInt();
    if (webServer.hasArg("fps"))           cfg.fps           = webServer.arg("fps").toInt();
    if (webServer.hasArg("xclkMhz"))       cfg.xclkMhz       = webServer.arg("xclkMhz").toInt();
    if (webServer.hasArg("visibleSecs"))   cfg.visibleSecs   = webServer.arg("visibleSecs").toInt();
    saveConfig();

    // If resolution or clock changed, reinit the camera immediately — no reboot needed.
    if (cfg.frameSize != oldFrameSize || cfg.xclkMhz != oldXclkMhz) {
      Serial.printf("[Camera] Settings changed (frameSize %u->%u, xclk %u->%u) — reinitializing...\n",
        oldFrameSize, cfg.frameSize, oldXclkMhz, cfg.xclkMhz);
      burstState = STATE_IDLE;
      if (cameraReinit()) {
        rtspBegin(camWidth, camHeight);
        Serial.printf("[Camera] Reinit OK — %ux%u\n", camWidth, camHeight);
      } else {
        Serial.println("[Camera] Reinit FAILED");
      }
    }

    webServer.send(200, "application/json", "{\"ok\":true}");
  });

  webServer.on("/trigger", HTTP_GET, []() {
    startBurst();
    webServer.send(200, "application/json", "{\"ok\":true}");
  });

  webServer.on("/clearbuffer", HTTP_GET, []() {
    bufferClear();
    burstState = STATE_IDLE;
    webServer.send(200, "application/json", "{\"ok\":true}");
  });

  webServer.on("/status", HTTP_GET, []() {
    const char* s =
      burstState == STATE_RECORDING ? "Recording" :
      burstState == STATE_LOOPING   ? "Looping"   : "Idle";
    char json[256];
    snprintf(json, sizeof(json),
      "{\"state\":\"%s\",\"frames\":%u,\"ip\":\"%s\",\"rssi\":%d,\"camOk\":%s,\"obsWs\":%s}",
      s, (unsigned)bufferFrameCount(),
      WiFi.localIP().toString().c_str(), WiFi.RSSI(),
      cameraOk() ? "true" : "false",
      obsWsReady() ? "true" : "false");
    webServer.send(200, "application/json", json);
  });

  webServer.on("/cam.jpg", HTTP_GET, []() {
    if (!cameraOk()) { webServer.send(503, "text/plain", "Camera not ready"); return; }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { webServer.send(503, "text/plain", "Frame grab failed"); return; }
    webServer.sendHeader("Cache-Control", "no-cache, no-store");
    webServer.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  webServer.on("/stream", HTTP_GET, []() {
    if (!cameraOk()) { webServer.send(503, "text/plain", "Camera not ready"); return; }
    WiFiClient client = webServer.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    uint32_t lastF = 0;
    while (client.connected()) {
      webServer.handleClient();
      ArduinoOTA.handle();
      if (millis() - lastF < interval) { delay(2); continue; }
      lastF = millis();
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) continue;
      client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
      client.write(fb->buf, fb->len);
      client.print("\r\n");
      esp_camera_fb_return(fb);
    }
  });

  webServer.begin();
  Serial.println("[Web] Server ready");
}

// ============================================================
//  setup / loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BurstCast] Boot");
  loadConfig();
  connectWiFi();
  if (!captivePortalMode) {
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();
    udpTrigger.begin(cfg.triggerPort);
    obsWsBegin();
  }
  setupWebServer();

  // Camera MUST init before rtspBegin() so camWidth/camHeight
  // reflect the actual pixel dimensions the sensor produces.
  if (!cameraInit()) {
    Serial.println("[BurstCast] Camera FAILED");
  }

  if (!captivePortalMode) {
    rtspBegin(camWidth, camHeight);
  }

  Serial.printf("[BurstCast] Ready — rtsp://%s:554/mjpeg/1\n",
    WiFi.localIP().toString().c_str());
}

void loop() {
  if (captivePortalMode) { dnsServer.processNextRequest(); }
  webServer.handleClient();
  if (!captivePortalMode) {
    ArduinoOTA.handle();
    rtspHandle();
    handleTrigger();
    handleRecording();
    handleObsHide();
    obsWsHandle();
    static uint32_t lastWifi = 0;
    if (millis() - lastWifi > 10000) {
      lastWifi = millis();
      if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    }
  }
}
