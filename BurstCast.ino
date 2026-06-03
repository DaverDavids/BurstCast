// ============================================================
//  BurstCast — ESP32-S3 WiFi-triggered burst camera → OBS RTP
//  Board: ESP32-S3 with OV2640 (or compatible) camera module
// ============================================================

// ---- Debug toggle (comment out to disable all Serial output) ----
#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
  #define DBG(x)    Serial.print(x)
  #define DBGLN(x)  Serial.println(x)
  #define DBGF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(x)
  #define DBGLN(x)
  #define DBGF(...)
#endif

// ---- Core libs ----
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_camera.h>

// ---- Secrets (not in repo) ----
#include <Secrets.h>
// Expects: #define MYSSIDIOT "your_ssid"
//          #define MYPSKIOT  "your_password"

// ---- Config struct + cfg instance (must be first) ----
#include "config.h"
// ---- Web UI ----
#include "html.h"

// ============================================================
//  Camera pin map (XIAO ESP32-S3 Sense)
//  Adjust CAM_PIN_* defines below for your specific module
// ============================================================
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    10
#define CAM_PIN_SIOD    40
#define CAM_PIN_SIOC    39
#define CAM_PIN_D7      48
#define CAM_PIN_D6      11
#define CAM_PIN_D5      12
#define CAM_PIN_D4      14
#define CAM_PIN_D3      16
#define CAM_PIN_D2      18
#define CAM_PIN_D1      17
#define CAM_PIN_D0      15
#define CAM_PIN_VSYNC   38
#define CAM_PIN_HREF    47
#define CAM_PIN_PCLK    13

// ============================================================
//  Globals
// ============================================================
Preferences prefs;
WebServer   webServer(80);
DNSServer   dnsServer;
WiFiUDP     udpTrigger;
WiFiUDP     udpRtp;

bool     captivePortalMode = false;
bool     burstActive       = false;
uint16_t framesSent        = 0;
uint16_t rtpSeq            = 0;
uint32_t rtpTimestamp      = 0;
const uint32_t rtpSsrc     = 0xBEEFCAFE;

// RTP uses 90 kHz clock; timestamp step = 90000 / fps
// Recalculated whenever cfg.fps changes via web UI
uint32_t rtpTimestampStep  = 90000 / DEFAULT_FPS;

// ============================================================
//  Preferences helpers
// ============================================================
void loadConfig() {
  prefs.begin("burstcast", true);
  strlcpy(cfg.obsIp, prefs.getString("obsIp", "").c_str(), sizeof(cfg.obsIp));
  cfg.obsPort     = prefs.getUShort("obsPort",     DEFAULT_OBS_PORT);
  cfg.triggerPort = prefs.getUShort("trigPort",    TRIGGER_PORT);
  cfg.burstFrames = prefs.getUShort("burstFrames", DEFAULT_BURST_FRAMES);
  cfg.jpegQuality = prefs.getUChar ("jpegQuality", DEFAULT_JPEG_QUALITY);
  cfg.frameSize   = prefs.getUChar ("frameSize",   DEFAULT_FRAME_SIZE);
  cfg.fps         = prefs.getUChar ("fps",         DEFAULT_FPS);
  cfg.xclkMhz     = prefs.getUChar ("xclkMhz",    DEFAULT_XCLK_MHZ);
  prefs.end();
  rtpTimestampStep = 90000 / (cfg.fps > 0 ? cfg.fps : 1);
  DBGLN("[Config] Loaded from flash");
}

void saveConfig() {
  prefs.begin("burstcast", false);
  prefs.putString("obsIp",       cfg.obsIp);
  prefs.putUShort("obsPort",     cfg.obsPort);
  prefs.putUShort("trigPort",    cfg.triggerPort);
  prefs.putUShort("burstFrames", cfg.burstFrames);
  prefs.putUChar ("jpegQuality", cfg.jpegQuality);
  prefs.putUChar ("frameSize",   cfg.frameSize);
  prefs.putUChar ("fps",         cfg.fps);
  prefs.putUChar ("xclkMhz",     cfg.xclkMhz);
  prefs.end();
  rtpTimestampStep = 90000 / (cfg.fps > 0 ? cfg.fps : 1);
  DBGLN("[Config] Saved to flash");
}

// ============================================================
//  Camera init
// ============================================================
bool initCamera() {
  camera_config_t cam;
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer   = LEDC_TIMER_0;
  cam.pin_d0       = CAM_PIN_D0;
  cam.pin_d1       = CAM_PIN_D1;
  cam.pin_d2       = CAM_PIN_D2;
  cam.pin_d3       = CAM_PIN_D3;
  cam.pin_d4       = CAM_PIN_D4;
  cam.pin_d5       = CAM_PIN_D5;
  cam.pin_d6       = CAM_PIN_D6;
  cam.pin_d7       = CAM_PIN_D7;
  cam.pin_xclk     = CAM_PIN_XCLK;
  cam.pin_pclk     = CAM_PIN_PCLK;
  cam.pin_vsync    = CAM_PIN_VSYNC;
  cam.pin_href     = CAM_PIN_HREF;
  cam.pin_sccb_sda = CAM_PIN_SIOD;
  cam.pin_sccb_scl = CAM_PIN_SIOC;
  cam.pin_pwdn     = CAM_PIN_PWDN;
  cam.pin_reset    = CAM_PIN_RESET;
  cam.xclk_freq_hz = (uint32_t)cfg.xclkMhz * 1000000;
  cam.pixel_format = PIXFORMAT_JPEG;
  cam.frame_size   = (framesize_t)cfg.frameSize;
  cam.jpeg_quality = cfg.jpegQuality;
  cam.fb_count     = 2;
  cam.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    DBGF("[Camera] Init failed: 0x%x\n", err);
    return false;
  }

  // Apply FPS via sensor registers after init
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // OV2640: set_framesize also programs PLL/clock dividers.
    // Explicitly set the frame rate divider for target FPS.
    // For most use cases CAMERA_GRAB_LATEST + fb_count=2 handles pacing;
    // the loop delay below is the primary FPS limiter for burst sending.
    s->set_framesize(s, (framesize_t)cfg.frameSize);
  }

  DBGF("[Camera] Init OK — XCLK=%dMHz, target FPS=%d\n", cfg.xclkMhz, cfg.fps);
  return true;
}

// ============================================================
//  WiFi — connect or fall back to captive portal
// ============================================================
void startCaptivePortal() {
  captivePortalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dnsServer.start(53, "*", WiFi.softAPIP());
  DBGF("[WiFi] Captive portal up: SSID='%s' IP=%s\n",
       AP_SSID, WiFi.softAPIP().toString().c_str());
}

void connectWiFi() {
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSIDIOT, MYPSKIOT);
  DBGF("[WiFi] Connecting to %s", MYSSIDIOT);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(250);
    DBG(".");
  }
  DBG("\n");
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setTxPower(WIFI_TX_POWER);
    DBGF("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    DBGLN("[WiFi] Failed — starting captive portal");
    startCaptivePortal();
  }
}

// ============================================================
//  mDNS + OTA
// ============================================================
void setupMDNS() {
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    DBGF("[mDNS] Advertising as http://%s.local\n", HOSTNAME);
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()             { DBGLN("[OTA] Start"); });
  ArduinoOTA.onEnd([]()               { DBGLN("[OTA] End"); });
  ArduinoOTA.onError([](ota_error_t e) { DBGF("[OTA] Error %u\n", e); });
  ArduinoOTA.begin();
  DBGLN("[OTA] Ready");
}

// ============================================================
//  RTP/JPEG packetizer (RFC 2435, PT=26)
// ============================================================
#define RTP_HDR_LEN     12
#define JPEG_HDR_LEN     8
#define MAX_RTP_PAYLOAD 1400
#define PKT_BUF_LEN     (RTP_HDR_LEN + JPEG_HDR_LEN + MAX_RTP_PAYLOAD)

void sendRtpJpeg(const uint8_t* jpegData, size_t jpegLen) {
  if (cfg.obsIp[0] == '\0') return;

  static uint8_t pkt[PKT_BUF_LEN];
  size_t offset = 0;

  while (offset < jpegLen) {
    size_t payloadLen = (jpegLen - offset) < MAX_RTP_PAYLOAD
                        ? (jpegLen - offset) : MAX_RTP_PAYLOAD;
    bool last = (offset + payloadLen >= jpegLen);

    // RTP header
    pkt[0]  = 0x80;
    pkt[1]  = (last ? 0x80 : 0x00) | 26;  // M bit + PT 26 (JPEG)
    pkt[2]  = (rtpSeq >> 8) & 0xFF;
    pkt[3]  =  rtpSeq & 0xFF;
    pkt[4]  = (rtpTimestamp >> 24) & 0xFF;
    pkt[5]  = (rtpTimestamp >> 16) & 0xFF;
    pkt[6]  = (rtpTimestamp >>  8) & 0xFF;
    pkt[7]  =  rtpTimestamp & 0xFF;
    pkt[8]  = (rtpSsrc >> 24) & 0xFF;
    pkt[9]  = (rtpSsrc >> 16) & 0xFF;
    pkt[10] = (rtpSsrc >>  8) & 0xFF;
    pkt[11] =  rtpSsrc & 0xFF;
    // JPEG header (RFC 2435 §3.1)
    pkt[12] = 0;
    pkt[13] = (offset >> 16) & 0xFF;
    pkt[14] = (offset >>  8) & 0xFF;
    pkt[15] =  offset & 0xFF;
    pkt[16] = 1;
    pkt[17] = cfg.jpegQuality;
    pkt[18] = 0;
    pkt[19] = 0;

    memcpy(pkt + RTP_HDR_LEN + JPEG_HDR_LEN, jpegData + offset, payloadLen);

    udpRtp.beginPacket(cfg.obsIp, cfg.obsPort);
    udpRtp.write(pkt, RTP_HDR_LEN + JPEG_HDR_LEN + payloadLen);
    udpRtp.endPacket();

    rtpSeq++;
    offset += payloadLen;
  }
  rtpTimestamp += rtpTimestampStep;  // 90 kHz clock, step = 90000/fps
}

// ============================================================
//  Burst
// ============================================================
void startBurst() {
  if (burstActive) return;
  burstActive = true;
  framesSent  = 0;
  DBGLN("[Burst] Started");
}

void handleBurst() {
  if (!burstActive) return;

  // Pace frame sends to configured FPS
  static unsigned long lastFrameMs = 0;
  unsigned long frameIntervalMs = (cfg.fps > 0) ? (1000UL / cfg.fps) : 66;
  if (millis() - lastFrameMs < frameIntervalMs) return;
  lastFrameMs = millis();

  if (framesSent >= cfg.burstFrames) {
    burstActive = false;
    DBGF("[Burst] Complete — %u frames sent\n", framesSent);
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { DBGLN("[Camera] Frame grab failed"); return; }
  sendRtpJpeg(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  framesSent++;
}

// ============================================================
//  UDP trigger listener
// ============================================================
void handleTrigger() {
  int pktSize = udpTrigger.parsePacket();
  if (pktSize <= 0) return;

  char buf[32];
  int readLen = pktSize < (int)(sizeof(buf) - 1) ? pktSize : (int)(sizeof(buf) - 1);
  udpTrigger.read(buf, readLen);
  buf[readLen] = '\0';
  DBGF("[Trigger] Received: %s\n", buf);

  startBurst();

  udpTrigger.beginPacket(udpTrigger.remoteIP(), udpTrigger.remotePort());
  udpTrigger.write((const uint8_t*)ACK_MSG, strlen(ACK_MSG));
  udpTrigger.endPacket();
  DBGF("[Trigger] ACK → %s\n", udpTrigger.remoteIP().toString().c_str());
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
    if (captivePortalMode)
      webServer.send(200, "text/html", FPSTR(CAPTIVE_FORM));
    else
      webServer.send(200, "text/html", buildConfigPage());
  });

  webServer.on("/save", HTTP_POST, []() {
    if (webServer.hasArg("obsIp"))       strlcpy(cfg.obsIp, webServer.arg("obsIp").c_str(), sizeof(cfg.obsIp));
    if (webServer.hasArg("obsPort"))     cfg.obsPort     = (uint16_t)webServer.arg("obsPort").toInt();
    if (webServer.hasArg("trigPort"))    cfg.triggerPort = (uint16_t)webServer.arg("trigPort").toInt();
    if (webServer.hasArg("burstFrames")) cfg.burstFrames = (uint16_t)webServer.arg("burstFrames").toInt();
    if (webServer.hasArg("jpegQuality")) cfg.jpegQuality = (uint8_t)webServer.arg("jpegQuality").toInt();
    if (webServer.hasArg("frameSize"))   cfg.frameSize   = (uint8_t)webServer.arg("frameSize").toInt();
    if (webServer.hasArg("fps"))         cfg.fps         = (uint8_t)webServer.arg("fps").toInt();
    if (webServer.hasArg("xclkMhz"))     cfg.xclkMhz     = (uint8_t)webServer.arg("xclkMhz").toInt();
    if (webServer.hasArg("ssid") && webServer.hasArg("psk")) {
      prefs.begin("wifi", false);
      prefs.putString("ssid", webServer.arg("ssid"));
      prefs.putString("psk",  webServer.arg("psk"));
      prefs.end();
    }
    saveConfig();
    webServer.send(200, "text/html",
      "<html><body style='font-family:monospace;background:#111;color:#eee;padding:20px'>"
      "<p>&#x2705; Saved. <a href='/' style='color:#f90'>Back</a></p>"
      "<p><small>Camera settings (XCLK, frame size) take effect after reboot.</small></p>"
      "</body></html>");
  });

  webServer.on("/trigger", HTTP_GET, []() {
    startBurst();
    webServer.send(200, "text/plain", "Burst triggered");
  });

  webServer.on("/status", HTTP_GET, []() {
    char json[256];
    snprintf(json, sizeof(json),
      "{\"burst\":%s,\"framesSent\":%u,\"ip\":\"%s\",\"rssi\":%d}",
      burstActive ? "true" : "false",
      (unsigned)framesSent,
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI());
    webServer.send(200, "application/json", json);
  });

  webServer.begin();
  DBGLN("[Web] Server started on port 80");
}

// ============================================================
//  WiFi reconnect watchdog
// ============================================================
void checkWiFi() {
  static unsigned long lastCheck = 0;
  if (captivePortalMode) return;
  if (millis() - lastCheck < 10000) return;
  lastCheck = millis();
  if (WiFi.status() != WL_CONNECTED) {
    DBGLN("[WiFi] Lost — reconnecting...");
    WiFi.reconnect();
  }
}

// ============================================================
//  setup() / loop()
// ============================================================
void setup() {
#ifdef DEBUG_ENABLED
  Serial.begin(115200);
  delay(500);
#endif
  DBGLN("\n[BurstCast] Booting...");

  loadConfig();
  connectWiFi();

  if (!captivePortalMode) {
    setupMDNS();
    setupOTA();
    udpTrigger.begin(cfg.triggerPort);
    DBGF("[UDP] Trigger listener on port %u\n", cfg.triggerPort);
  }

  setupWebServer();
  initCamera();

  DBGLN("[BurstCast] Ready");
}

void loop() {
  if (captivePortalMode) dnsServer.processNextRequest();
  webServer.handleClient();
  if (!captivePortalMode) {
    ArduinoOTA.handle();
    handleTrigger();
    handleBurst();
    checkWiFi();
  }
}
