#pragma once

// ============================================================
//  BurstCast — runtime config (saved to NVS via Preferences)
// ============================================================

#define HOSTNAME          "burstcast"
#define AP_SSID           "BurstCast-Setup"
#define WIFI_TIMEOUT_MS   10000
#define WIFI_TX_POWER     WIFI_POWER_11dBm
#define ACK_MSG           "OK"

#define TRIGGER_PORT        5005
#define DEFAULT_OBS_IP      ""
#define DEFAULT_OBS_WS_PORT 4455
#define DEFAULT_OBS_WS_PASS ""
#define DEFAULT_SCENE_NAME  ""
#define DEFAULT_SOURCE_NAME "BurstCast"

#define DEFAULT_BURST_FRAMES  45      // frames to record per trigger
#define DEFAULT_FPS           15
#define DEFAULT_JPEG_QUALITY  12      // 0=best, 63=worst
#define DEFAULT_FRAME_SIZE    6       // FRAMESIZE_VGA
#define DEFAULT_XCLK_MHZ      16
#define DEFAULT_VISIBLE_SECS  0       // 0 = match clip duration automatically

// States
typedef enum {
  STATE_IDLE,        // no frames recorded yet, RTSP sends live passthrough
  STATE_RECORDING,   // capturing frames into PSRAM buffer
  STATE_LOOPING,     // serving PSRAM buffer in a loop over RTSP
} BurstState;

struct Config {
  // Network / OBS (legacy fields kept for NVS compat)
  char     obsIp[40];
  uint16_t obsPort;
  uint16_t triggerPort;

  // OBS WebSocket
  char     obsWsIp[40];
  uint16_t obsWsPort;
  char     obsWsPass[64];
  char     obsSceneName[64];
  char     obsSourceName[64];

  // Camera / burst
  uint16_t burstFrames;
  uint8_t  jpegQuality;
  uint8_t  frameSize;
  uint8_t  fps;
  uint8_t  xclkMhz;

  // OBS source visibility
  // 0 = auto (source stays visible for exactly the clip duration)
  // >0 = manual override in whole seconds
  uint16_t visibleSecs;
};

inline Config cfg = {
  .obsIp         = "",
  .obsPort       = 0,
  .triggerPort   = TRIGGER_PORT,
  .obsWsIp       = "",
  .obsWsPort     = DEFAULT_OBS_WS_PORT,
  .obsWsPass     = "",
  .obsSceneName  = "",
  .obsSourceName = DEFAULT_SOURCE_NAME,
  .burstFrames   = DEFAULT_BURST_FRAMES,
  .jpegQuality   = DEFAULT_JPEG_QUALITY,
  .frameSize     = DEFAULT_FRAME_SIZE,
  .fps           = DEFAULT_FPS,
  .xclkMhz       = DEFAULT_XCLK_MHZ,
  .visibleSecs   = DEFAULT_VISIBLE_SECS,
};

inline BurstState burstState = STATE_IDLE;
