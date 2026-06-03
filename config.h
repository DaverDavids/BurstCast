#pragma once
// ============================================================
//  config.h — BurstCast shared config struct
//  Included FIRST by both BurstCast.ino and html.h
// ============================================================

#define HOSTNAME         "burstcast"
#define WIFI_TX_POWER    WIFI_POWER_11dBm
#define WIFI_TIMEOUT_MS  15000
#define TRIGGER_PORT     5555
#define ACK_MSG          "BURSTCAST_ACK"
#define AP_SSID          "BurstCast-Setup"

#define DEFAULT_BURST_FRAMES  60
#define DEFAULT_OBS_PORT      8554
#define DEFAULT_FRAME_SIZE    7     // FRAMESIZE_VGA = 7
#define DEFAULT_JPEG_QUALITY  12

struct Config {
  char     obsIp[32];
  uint16_t obsPort;
  uint16_t triggerPort;
  uint16_t burstFrames;
  uint8_t  jpegQuality;
  uint8_t  frameSize;
};

extern Config cfg;
