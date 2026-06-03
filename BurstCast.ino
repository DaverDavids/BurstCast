// ============================================================
//  BurstCast — ESP32-S3 WiFi-triggered burst camera → OBS RTSP
//  Board: ESP32-S3 with OV2640 (or compatible) camera module
// ============================================================

// ---- Debug toggle (comment out to disable all Serial output) ----
#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
  #define DBGF(...)Serial.printf(__VA_ARGS__)
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

// ---- Config ----
#include "html.h"

// ============================================================
//  Constants & defaults
// ============================================================
#define HOSTNAME        "burstcast"
#define WIFI_TX_DBM     11          // dBm after connect
#define WIFI_TIMEOUT_MS 15000       // ms before falling back to AP/captive portal
#define TRIGGER_PORT    5555        // UDP port this device listens on for trigger
#define ACK_MSG         "BURSTCAST_ACK"
#define AP_SSID         "BurstCast-Setup"

// Default configurable values (overridden by Preferences)
#define DEFAULT_BURST_FRAMES  60
#define DEFAULT_OBS_PORT      8554  // OBS RTSP ingest port
#define DEFAULT_FRAME_SIZE    FRAMESIZE_VGA
#define DEFAULT_JPEG_QUALITY  12    // lower = better (OV2640 range 0-63)

// ---- Camera pin map (XIAO ESP32-S3 Sense / AI-Thinker compatible) ----
// Adjust to your specific module if needed
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     10
#define CAM_PIN_SIOD     40
#define CAM_PIN_SIOC     39
#define CAM_PIN_D7       48
#define CAM_PIN_D6       11
#define CAM_PIN_D5       12
#define CAM_PIN_D4       14
#define CAM_PIN_D3       16
#define CAM_PIN_D2        18
#define CAM_PIN_D1       17
#define CAM_PIN_D0        15
#define CAM_PIN_VSYNC    38
#define CAM_PIN_HREF     47
#define CAM_PIN_PCLK     13

// ============================================================
//  Globals
// ============================================================
Preferences  prefs;
WebServer    webServer(80);
DNSServer    dnsServer;
WiFiUDP      udpTrigger;
WiFiUDP      udpRtp;          // RTP stream to OBS

// Runtime config (loaded from flash, editable via web UI)
struct Config {
  char obsIp[32]       = "";
  uint16_t obsPort     = DEFAULT_OBS_PORT;
  uint16_t triggerPort = TRIGGER_PORT;
  uint16_t burstFrames = DEFAULT_BURST_FRAMES;
  uint8_t  jpegQuality = DEFAULT_JPEG_QUALITY;
  uint8_t  frameSize   = DEFAULT_FRAME_SIZE;   // sensor_t framesize enum
} cfg;

bool     captivePortalMode = false;
bool     burstActive       = false;
uint16_t framesSent        = 0;
uint16_t rtpSeq            = 0;
uint32_t rtpTimestamp      = 0;
uint32_t rtpSsrc           = 0xBEEFCAFE;

// ============================================================
//  Preferences helpers
// ============================================================
void loadConfig() {
  prefs.begin("burstcast", true);
  strlcpy(cfg.obsIp,       prefs.getString("obsIp",      "").c_str(), sizeof(cfg.obsIp));
  cfg.obsPort      = prefs.getUShort("obsPort",     DEFAULT_OBS_PORT);
  cfg.triggerPort  = prefs.getUShort("trigPort",    TRIGGER_PORT);
  cfg.burstFrames  = prefs.getUShort("burstFrames", DEFAULT_BURST_FRAMES);
  cfg.jpegQuality  = prefs.getUChar("jpegQuality",  DEFAULT_JPEG_QUALITY);
  cfg.frameSize    = prefs.getUChar("frameSize",    DEFAULT_FRAME_SIZE);
  prefs.end();
  DBGLN("[Config] Loaded from flash");
}

void saveConfig() {
  prefs.begin("burstcast", false);
  prefs.putString("obsIp",      cfg.obsIp);
  prefs.putUShort("obsPort",    cfg.obsPort);
  prefs.putUShort("trigPort",   cfg.triggerPort);
  prefs.putUShort("burstFrames",cfg.burstFrames);
  prefs.putUChar("jpegQuality", cfg.jpegQuality);
  prefs.putUChar("frameSize",   cfg.frameSize);
  prefs.end();
  DBGLN("[Config] Saved to flash");
}

// ============================================================
//  Camera init
// ============================================================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;
  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = (framesize_t)cfg.frameSize;
  config.jpeg_quality = cfg.jpegQuality;
  config.fb_count     = 2;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DBGF("[Camera] Init failed: 0x%x\n", err);
    return false;
  }
  DBGLN("[Camera] Init OK");
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
  DBGF("[WiFi] Captive portal: connect to '%s' → http://%s\n",
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
    WiFi.setTxPower((wifi_power_t)WIFI_TX_DBM);
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
    DBGF("[mDNS] http://%s.local\n", HOSTNAME);
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("[OTA] Start"); });
  ArduinoOTA.onEnd([]()    { DBGLN("[OTA] End"); });
  ArduinoOTA.onError([](ota_error_t e) { DBGF("[OTA] Error %u\n", e); });
  ArduinoOTA.begin();
  DBGLN("[OTA] Ready");
}

// ============================================================
//  RTP helpers (JPEG-over-RTP, RFC 2435)
// ============================================================
void sendRtpJpeg(const uint8_t* jpegData, size_t jpegLen) {
  if (strlen(cfg.obsIp) == 0) return;

  // Simple RTP/JPEG packetizer — max payload per packet
  const size_t MAX_PAYLOAD = 1400;
  size_t offset = 0;
  bool   first  = true;

  while (offset < jpegLen) {
    size_t payloadLen = min(MAX_PAYLOAD, jpegLen - offset);
    bool   last       = (offset + payloadLen >= jpegLen);

    // 12-byte RTP header + 8-byte JPEG header
    uint8_t pkt[12 + 8 + MAX_PAYLOAD];
    // RTP header
    pkt[0] = 0x80;  // V=2, P=0, X=0, CC=0
    pkt[1] = (last ? 0x80 : 0x00) | 26; // M bit on last pkt, PT=26 (JPEG)
    pkt[2] = (rtpSeq >> 8) & 0xFF;
    pkt[3] =  rtpSeq & 0xFF;
    pkt[4] = (rtpTimestamp >> 24) & 0xFF;
    pkt[5] = (rtpTimestamp >> 16) & 0xFF;
    pkt[6] = (rtpTimestamp >>  8) & 0xFF;
    pkt[7] =  rtpTimestamp & 0xFF;
    pkt[8] = (rtpSsrc >> 24) & 0xFF;
    pkt[9] = (rtpSsrc >> 16) & 0xFF;
    pkt[10]= (rtpSsrc >>  8) & 0xFF;
    pkt[11]=  rtpSsrc & 0xFF;
    // JPEG header (RFC 2435 §3.1)
    pkt[12] = 0; // Type-specific
    pkt[13] = (offset >> 16) & 0xFF; // Fragment offset (3 bytes)
    pkt[14] = (offset >>  8) & 0xFF;
    pkt[15] =  offset & 0xFF;
    pkt[16] = 1;   // Type = 1 (JPEG baseline)
    pkt[17] = cfg.jpegQuality;
    pkt[18] = 0;   // Width in 8-pixel blocks (0 = parse from JPEG)
    pkt[19] = 0;   // Height in 8-pixel blocks

    memcpy(pkt + 20, jpegData + offset, payloadLen);
    udpRtp.beginPacket(cfg.obsIp, cfg.obsPort);
    udpRtp.write(pkt, 20 + payloadLen);
    udpRtp.endPacket();

    rtpSeq++;
    offset += payloadLen;
    first = false;
  }
  // ~90 kHz RTP clock for video, ~3003 ticks per 30fps frame
  rtpTimestamp += 3003;
}

// ============================================================
//  Burst recording & streaming
// ============================================================
void startBurst() {
  if (burstActive) return;
  burstActive = true;
  framesSent  = 0;
  DBGLN("[Burst] Started");
}

void handleBurst() {
  if (!burstActive) return;
  if (framesSent >= cfg.burstFrames) {
    burstActive = false;
    DBGF("[Burst] Done — %u frames sent\n", framesSent);
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
  udpTrigger.read(buf, sizeof(buf) - 1);
  buf[pktSize] = '\0';
  DBGF("[Trigger] Received: %s\n", buf);
  // Any incoming UDP packet on trigger port starts a burst
  startBurst();
  // Send ACK back to sender
  udpTrigger.beginPacket(udpTrigger.remoteIP(), udpTrigger.remotePort());
  udpTrigger.write((const uint8_t*)ACK_MSG, strlen(ACK_MSG));
  udpTrigger.endPacket();
  DBGF("[Trigger] ACK sent to %s\n", udpTrigger.remoteIP().toString().c_str());
}

// ============================================================
//  Web server routes
// ============================================================
void setupWebServer() {
  // Captive portal redirect
  webServer.onNotFound([]() {
    if (captivePortalMode) {
      webServer.sendHeader("Location", "http://192.168.4.1/");
      webServer.send(302, "text/plain", "");
    } else {
      webServer.send(404, "text/plain", "Not found");
    }
  });

  // Config page (GET)
  webServer.on("/", HTTP_GET, []() {
    webServer.send(200, "text/html", buildConfigPage());
  });

  // Save config (POST)
  webServer.on("/save", HTTP_POST, []() {
    if (webServer.hasArg("obsIp"))      strlcpy(cfg.obsIp, webServer.arg("obsIp").c_str(), sizeof(cfg.obsIp));
    if (webServer.hasArg("obsPort"))    cfg.obsPort      = webServer.arg("obsPort").toInt();
    if (webServer.hasArg("trigPort"))   cfg.triggerPort  = webServer.arg("trigPort").toInt();
    if (webServer.hasArg("burstFrames"))cfg.burstFrames  = webServer.arg("burstFrames").toInt();
    if (webServer.hasArg("jpegQuality"))cfg.jpegQuality  = webServer.arg("jpegQuality").toInt();
    if (webServer.hasArg("frameSize"))  cfg.frameSize    = webServer.arg("frameSize").toInt();
    // Captive portal WiFi credentials
    if (webServer.hasArg("ssid") && webServer.hasArg("psk")) {
      prefs.begin("wifi", false);
      prefs.putString("ssid", webServer.arg("ssid"));
      prefs.putString("psk",  webServer.arg("psk"));
      prefs.end();
    }
    saveConfig();
    webServer.send(200, "text/html",
      "<html><body><p>Saved! <a href='/'>Back</a></p>"
      "<p><small>Reboot to apply WiFi changes.</small></p></body></html>");
  });

  // Manual trigger endpoint (for testing)
  webServer.on("/trigger", HTTP_GET, []() {
    startBurst();
    webServer.send(200, "text/plain", "Burst triggered");
  });

  // Status JSON
  webServer.on("/status", HTTP_GET, []() {
    char json[256];
    snprintf(json, sizeof(json),
      "{\"burst\":%s,\"framesSent\":%u,\"ip\":\"%s\",\"rssi\":%d}",
      burstActive ? "true" : "false",
      framesSent,
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
    DBGLN("[WiFi] Lost connection — reconnecting...");
    WiFi.reconnect();
  }
}

// ============================================================
//  setup()
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
    DBGF("[UDP] Listening for triggers on port %u\n", cfg.triggerPort);
  }

  setupWebServer();
  initCamera();

  DBGLN("[BurstCast] Ready");
}

// ============================================================
//  loop()
// ============================================================
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
