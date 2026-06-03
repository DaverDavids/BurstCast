#pragma once
// ============================================================
//  config.h — BurstCast shared config struct + single instance
//  Include this ONCE before any other BurstCast headers.
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
#define DEFAULT_FPS           15
#define DEFAULT_XCLK_MHZ      20

struct Config {
  char     obsIp[32];
  uint16_t obsPort;
  uint16_t triggerPort;
  uint16_t burstFrames;
  uint8_t  jpegQuality;
  uint8_t  frameSize;
  uint8_t  fps;          // target capture FPS (also sets RTP timestamp step)
  uint8_t  xclkMhz;     // camera XCLK frequency in MHz (8, 16, 20, 24)
};

// Single global instance — inline ensures one definition across all TUs (C++17)
inline Config cfg = {
  "",                   // obsIp
  DEFAULT_OBS_PORT,     // obsPort
  TRIGGER_PORT,         // triggerPort
  DEFAULT_BURST_FRAMES, // burstFrames
  DEFAULT_JPEG_QUALITY, // jpegQuality
  DEFAULT_FRAME_SIZE,   // frameSize
  DEFAULT_FPS,          // fps
  DEFAULT_XCLK_MHZ      // xclkMhz
};
