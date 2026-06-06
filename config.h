#pragma once

// ============================================================
//  BurstCast — runtime config (saved to NVS via Preferences)
// ============================================================

#define HOSTNAME          "burstcast"
#define AP_SSID           "BurstCast-Setup"
#define WIFI_TIMEOUT_MS   20000
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

// Sensor tuning defaults (int8 range matches esp_camera sensor API)
#define DEFAULT_CAM_BRIGHTNESS    0   // -2..2
#define DEFAULT_CAM_CONTRAST      0   // -2..2
#define DEFAULT_CAM_SATURATION    0   // -2..2
#define DEFAULT_CAM_SHARPNESS     0   // -2..2
#define DEFAULT_CAM_DENOISE       0   // 0..8
#define DEFAULT_CAM_AEC           1   // 1=auto exposure on, 0=manual
#define DEFAULT_CAM_AEC_VAL       300 // manual exposure (0-1200)
#define DEFAULT_CAM_GAIN          1   // 1=auto gain on, 0=manual
#define DEFAULT_CAM_GAIN_CTRL     0   // manual gain (0-30)
#define DEFAULT_CAM_AWB           1   // 1=auto white balance on
#define DEFAULT_CAM_AWB_GAIN      1   // 1=AWB gain enabled
#define DEFAULT_CAM_WB_MODE       0   // 0=auto,1=sunny,2=cloudy,3=office,4=home
#define DEFAULT_CAM_VFLIP         0
#define DEFAULT_CAM_HFLIP         0
#define DEFAULT_CAM_LENC          1   // lens correction
#define DEFAULT_CAM_DCW           1   // downsize EN

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
  uint16_t visibleSecs;

  // Sensor tuning
  int8_t   camBrightness;   // -2..2
  int8_t   camContrast;     // -2..2
  int8_t   camSaturation;   // -2..2
  int8_t   camSharpness;    // -2..2
  uint8_t  camDenoise;      // 0..8
  uint8_t  camAec;          // 1=auto exposure
  uint16_t camAecVal;       // manual exposure value 0-1200
  uint8_t  camGain;         // 1=auto gain
  uint8_t  camGainCtrl;     // manual gain 0-30
  uint8_t  camAwb;          // 1=auto white balance
  uint8_t  camAwbGain;      // 1=AWB gain enabled
  uint8_t  camWbMode;       // 0=auto,1=sunny,2=cloudy,3=office,4=home
  uint8_t  camVflip;        // 0/1
  uint8_t  camHflip;        // 0/1
  uint8_t  camLenc;         // lens correction 0/1
  uint8_t  camDcw;          // downsize EN 0/1
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
  .camBrightness = DEFAULT_CAM_BRIGHTNESS,
  .camContrast   = DEFAULT_CAM_CONTRAST,
  .camSaturation = DEFAULT_CAM_SATURATION,
  .camSharpness  = DEFAULT_CAM_SHARPNESS,
  .camDenoise    = DEFAULT_CAM_DENOISE,
  .camAec        = DEFAULT_CAM_AEC,
  .camAecVal     = DEFAULT_CAM_AEC_VAL,
  .camGain       = DEFAULT_CAM_GAIN,
  .camGainCtrl   = DEFAULT_CAM_GAIN_CTRL,
  .camAwb        = DEFAULT_CAM_AWB,
  .camAwbGain    = DEFAULT_CAM_AWB_GAIN,
  .camWbMode     = DEFAULT_CAM_WB_MODE,
  .camVflip      = DEFAULT_CAM_VFLIP,
  .camHflip      = DEFAULT_CAM_HFLIP,
  .camLenc       = DEFAULT_CAM_LENC,
  .camDcw        = DEFAULT_CAM_DCW,
};

inline BurstState burstState = STATE_IDLE;
