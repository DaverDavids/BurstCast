// ============================================================
//  BurstCast — ESP32-S3 WiFi-triggered burst camera → OBS RTP
//  Board: Freenove ESP32-S3-WROOM (N16R8 clone) + OV3660
// ============================================================

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

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_camera.h>

#include <Secrets.h>
#include "config.h"
#include "html.h"

// ============================================================
//  Camera pin map — Freenove ESP32-S3-WROOM / N16R8 clone
//  OV3660 sensor, dual USB-C
//  If probe still fails, swap SIOD/SIOC: try 1/2 or 8/9
// ============================================================
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5
#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      10
#define CAM_PIN_D2       8
#define CAM_PIN_D1       9
#define CAM_PIN_D0      11
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
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
bool     cameraOk          = false;
uint16_t framesSent        = 0;
uint16_t rtpSeq            = 0;
uint32_t rtpTimestamp      = 0;
const uint32_t rtpSsrc     = 0xBEEFCAFE;
uint32_t rtpTimestampStep  = 90000 / DEFAULT_FPS;

static const uint16_t FRAME_W[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
static const uint16_t FRAME_H[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

// ============================================================
//  Preferences
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
  DBGLN("[Config] Saved");
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
  // Init at QVGA; set configured size after sensor is known
  cam.frame_size   = FRAMESIZE_QVGA;
  cam.jpeg_quality = cfg.jpegQuality;
  cam.fb_count     = 2;
  cam.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    DBGF("[Camera] Init FAILED: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    DBGF("[Camera] Sensor PID: 0x%x\n", s->id.PID);
    // OV3660 is typically mounted upside-down on these modules
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_framesize(s, (framesize_t)cfg.frameSize);
  }

  DBGF("[Camera] OK — XCLK=%dMHz size=%d\n", cfg.xclkMhz, cfg.frameSize);
  return true;
}

// ============================================================
//  SDP builder
// ============================================================
String buildSdp() {
  uint8_t  fs = cfg.frameSize < 13 ? cfg.frameSize : 7;
  uint16_t w  = FRAME_W[fs];
  uint16_t h  = FRAME_H[fs];
  String sdp;
  sdp.reserve(300);
  sdp += "v=0\r\n";
  sdp += "o=- 0 0 IN IP4 "; sdp += WiFi.localIP().toString(); sdp += "\r\n";
  sdp += "s=BurstCast\r\n";
  sdp += "c=IN IP4 ";       sdp += WiFi.localIP().toString(); sdp += "\r\n";
  sdp += "t=0 0\r\n";
  sdp += "m=video ";        sdp += String(cfg.obsPort); sdp += " RTP/AVP 26\r\n";
  sdp += "a=rtpmap:26 JPEG/90000\r\n";
  sdp += "a=fmtp:26 width="; sdp += String(w); sdp += ";height="; sdp += String(h); sdp += "\r\n";
  sdp += "a=framerate:";    sdp += String(cfg.fps); sdp += "\r\n";
  return sdp;
}

// ============================================================
//  WiFi
// ============================================================
void startCaptivePortal() {
  captivePortalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dnsServer.start(53, "*", WiFi.softAPIP());
  DBGF("[WiFi] Captive portal: %s\n", WiFi.softAPIP().toString().c_str());
}

void connectWiFi() {
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSIDIOT, MYPSKIOT);
  DBGF("[WiFi] Connecting to %s", MYSSIDIOT);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(250); DBG(".");
  }
  DBG("\n");
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setTxPower(WIFI_TX_POWER);
    DBGF("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    DBGLN("[WiFi] Failed — captive portal");
    startCaptivePortal();
  }
}

void setupMDNS() {
  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); }
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onError([](ota_error_t e) { DBGF("[OTA] Error %u\n", e); });
  ArduinoOTA.begin();
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
    size_t payloadLen = min((size_t)MAX_RTP_PAYLOAD, jpegLen - offset);
    bool last = (offset + payloadLen >= jpegLen);
    pkt[0]  = 0x80;
    pkt[1]  = (last ? 0x80 : 0x00) | 26;
    pkt[2]  = (rtpSeq >> 8) & 0xFF;        pkt[3]  = rtpSeq & 0xFF;
    pkt[4]  = (rtpTimestamp >> 24) & 0xFF;  pkt[5]  = (rtpTimestamp >> 16) & 0xFF;
    pkt[6]  = (rtpTimestamp >>  8) & 0xFF;  pkt[7]  = rtpTimestamp & 0xFF;
    pkt[8]  = (rtpSsrc >> 24) & 0xFF;      pkt[9]  = (rtpSsrc >> 16) & 0xFF;
    pkt[10] = (rtpSsrc >>  8) & 0xFF;      pkt[11] = rtpSsrc & 0xFF;
    pkt[12] = 0;
    pkt[13] = (offset >> 16) & 0xFF; pkt[14] = (offset >> 8) & 0xFF; pkt[15] = offset & 0xFF;
    pkt[16] = 1; pkt[17] = cfg.jpegQuality; pkt[18] = 0; pkt[19] = 0;
    memcpy(pkt + RTP_HDR_LEN + JPEG_HDR_LEN, jpegData + offset, payloadLen);
    udpRtp.beginPacket(cfg.obsIp, cfg.obsPort);
    udpRtp.write(pkt, RTP_HDR_LEN + JPEG_HDR_LEN + payloadLen);
    udpRtp.endPacket();
    rtpSeq++;
    offset += payloadLen;
  }
  rtpTimestamp += rtpTimestampStep;
}

// ============================================================
//  Burst
// ============================================================
void startBurst() {
  if (burstActive || !cameraOk) return;
  burstActive = true;
  framesSent  = 0;
  DBGLN("[Burst] Started");
}

void handleBurst() {
  if (!burstActive) return;
  static unsigned long lastFrameMs = 0;
  unsigned long interval = (cfg.fps > 0) ? (1000UL / cfg.fps) : 66;
  if (millis() - lastFrameMs < interval) return;
  lastFrameMs = millis();
  if (framesSent >= cfg.burstFrames) {
    burstActive = false;
    DBGF("[Burst] Done — %u frames\n", framesSent);
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { DBGLN("[Camera] Frame grab failed"); return; }
  sendRtpJpeg(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  framesSent++;
}

// ============================================================
//  UDP trigger
// ============================================================
void handleTrigger() {
  int pktSize = udpTrigger.parsePacket();
  if (pktSize <= 0) return;
  char buf[32];
  int n = min(pktSize, (int)(sizeof(buf) - 1));
  udpTrigger.read(buf, n); buf[n] = '\0';
  DBGF("[Trigger] UDP: %s\n", buf);
  startBurst();
  udpTrigger.beginPacket(udpTrigger.remoteIP(), udpTrigger.remotePort());
  udpTrigger.write((const uint8_t*)ACK_MSG, strlen(ACK_MSG));
  udpTrigger.endPacket();
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
      webServer.send(200, "text/html", buildConfigPage(cameraOk));
  });

  webServer.on("/save", HTTP_POST, []() {
    if (webServer.hasArg("obsIp"))       strlcpy(cfg.obsIp, webServer.arg("obsIp").c_str(), sizeof(cfg.obsIp));
    if (webServer.hasArg("obsPort"))     cfg.obsPort     = (uint16_t)webServer.arg("obsPort").toInt();
    if (webServer.hasArg("trigPort"))    cfg.triggerPort = (uint16_t)webServer.arg("trigPort").toInt();
    if (webServer.hasArg("burstFrames")) cfg.burstFrames = (uint16_t)webServer.arg("burstFrames").toInt();
    if (webServer.hasArg("jpegQuality")) cfg.jpegQuality = (uint8_t) webServer.arg("jpegQuality").toInt();
    if (webServer.hasArg("frameSize"))   cfg.frameSize   = (uint8_t) webServer.arg("frameSize").toInt();
    if (webServer.hasArg("fps"))         cfg.fps         = (uint8_t) webServer.arg("fps").toInt();
    if (webServer.hasArg("xclkMhz"))     cfg.xclkMhz     = (uint8_t) webServer.arg("xclkMhz").toInt();
    saveConfig();
    webServer.send(200, "application/json", "{\"ok\":true}");
  });

  webServer.on("/trigger", HTTP_GET, []() {
    startBurst();
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"camOk\":%s}",
      cameraOk ? "true" : "false", cameraOk ? "true" : "false");
    webServer.send(200, "application/json", resp);
  });

  webServer.on("/status", HTTP_GET, []() {
    char json[320];
    snprintf(json, sizeof(json),
      "{\"burst\":%s,\"framesSent\":%u,\"ip\":\"%s\",\"rssi\":%d,\"camOk\":%s}",
      burstActive ? "true" : "false",
      (unsigned)framesSent,
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI(),
      cameraOk ? "true" : "false");
    webServer.send(200, "application/json", json);
  });

  webServer.on("/stream.sdp", HTTP_GET, []() {
    webServer.sendHeader("Content-Disposition", "attachment; filename=burstcast.sdp");
    webServer.send(200, "application/sdp", buildSdp());
  });

  // Single JPEG snapshot — used by web UI <img> refresh
  webServer.on("/cam.jpg", HTTP_GET, []() {
    if (!cameraOk) { webServer.send(503, "text/plain", "Camera not available"); return; }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb)      { webServer.send(503, "text/plain", "Frame grab failed"); return; }
    webServer.sendHeader("Cache-Control", "no-cache, no-store");
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  // Multipart MJPEG — open in browser tab or VLC: http://<ip>/stream
  webServer.on("/stream", HTTP_GET, []() {
    if (!cameraOk) { webServer.send(503, "text/plain", "Camera not available"); return; }
    WiFiClient client = webServer.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();
    unsigned long lastFrame = 0;
    unsigned long interval  = (cfg.fps > 0) ? (1000UL / cfg.fps) : 66;
    while (client.connected()) {
      webServer.handleClient();
      ArduinoOTA.handle();
      if (millis() - lastFrame < interval) { delay(2); continue; }
      lastFrame = millis();
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) continue;
      client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
      client.write(fb->buf, fb->len);
      client.print("\r\n");
      esp_camera_fb_return(fb);
    }
    DBGLN("[Stream] Client disconnected");
  });

  webServer.begin();
  DBGLN("[Web] Server ready");
}

void checkWiFi() {
  static unsigned long lastCheck = 0;
  if (captivePortalMode || millis() - lastCheck < 10000) return;
  lastCheck = millis();
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); }
}

// ============================================================
//  setup / loop
// ============================================================
void setup() {
#ifdef DEBUG_ENABLED
  Serial.begin(115200);
  delay(500);
#endif
  DBGLN("\n[BurstCast] Boot");
  loadConfig();
  connectWiFi();
  if (!captivePortalMode) {
    setupMDNS();
    setupOTA();
    udpTrigger.begin(cfg.triggerPort);
  }
  setupWebServer();
  cameraOk = initCamera();
  DBGF("[BurstCast] Ready — camera %s\n", cameraOk ? "OK" : "FAILED");
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
