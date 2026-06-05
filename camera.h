#pragma once
// ============================================================
//  camera.h — PSRAM frame buffer, capture, loop playback
// ============================================================
#include <esp_camera.h>
#include <vector>
#include "config.h"

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

static const framesize_t FRAMESIZE_MAP[] = {
  FRAMESIZE_QQVGA, FRAMESIZE_QCIF,  FRAMESIZE_HQVGA, FRAMESIZE_240X240,
  FRAMESIZE_QVGA,  FRAMESIZE_CIF,   FRAMESIZE_HVGA,  FRAMESIZE_VGA,
  FRAMESIZE_SVGA,  FRAMESIZE_XGA,   FRAMESIZE_HD,    FRAMESIZE_SXGA,
  FRAMESIZE_UXGA,
};
#define FRAMESIZE_MAP_COUNT 13

struct FrameEntry {
  uint8_t* buf;
  size_t   len;
  uint32_t captureMs;
};

static std::vector<FrameEntry> frameBuffer;
static volatile uint16_t       playbackIdx = 0;
static volatile bool           camReady    = false;

uint16_t camWidth  = 640;
uint16_t camHeight = 480;

static bool jpegSOFDims(const uint8_t* buf, size_t len, uint16_t* w, uint16_t* h) {
  const uint8_t* p   = buf + 2;
  const uint8_t* end = buf + len;
  while (p + 4 <= end) {
    if (p[0] != 0xFF) break;
    uint8_t m = p[1]; p += 2;
    if (m == 0xD9) break;
    if (m >= 0xD0 && m <= 0xD8) continue;
    if (p + 2 > end) break;
    uint16_t sl = (p[0] << 8) | p[1];
    if (sl < 2 || p + sl > end) break;
    if ((m == 0xC0 || m == 0xC2) && sl >= 9) {
      *h = (p[3] << 8) | p[4];
      *w = (p[5] << 8) | p[6];
      return true;
    }
    if (m == 0xDA) break;
    p += sl;
  }
  return false;
}

// Apply all cfg.camXxx sensor tuning fields to the live sensor.
// Safe to call any time after cameraInit() succeeds.
inline bool applySensorSettings() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  s->set_brightness(s,  cfg.camBrightness);
  s->set_contrast(s,    cfg.camContrast);
  s->set_saturation(s,  cfg.camSaturation);
  s->set_sharpness(s,   cfg.camSharpness);
  s->set_denoise(s,     cfg.camDenoise);
  s->set_exposure_ctrl(s, cfg.camAec);
  if (!cfg.camAec) s->set_aec_value(s, cfg.camAecVal);
  s->set_gain_ctrl(s,   cfg.camGain);
  if (!cfg.camGain) s->set_agc_gain(s, cfg.camGainCtrl);
  // set_whitebal omitted — unsafe at runtime, corrupts OV color matrix
  s->set_awb_gain(s,    cfg.camAwbGain);
  s->set_wb_mode(s,     cfg.camWbMode);
  s->set_vflip(s,       cfg.camVflip);
  s->set_hmirror(s,     cfg.camHflip);
  s->set_lenc(s,        cfg.camLenc);
  // set_dcw omitted — only takes effect at init, causes lockup at runtime
  Serial.println("[Camera] Sensor settings applied");
  return true;
}

inline bool cameraInit(bool isReinit = false) {
  Serial.printf("[Camera] frameSize=%u (enum %u) xclkMhz=%u quality=%u\n",
    cfg.frameSize,
    cfg.frameSize < FRAMESIZE_MAP_COUNT ? (uint8_t)FRAMESIZE_MAP[cfg.frameSize] : 0xff,
    cfg.xclkMhz, cfg.jpegQuality);

  if (!psramFound())
    Serial.println("[Camera] WARNING: PSRAM not found!");
  else
    Serial.printf("[Camera] PSRAM free: %u KB\n", ESP.getFreePsram() / 1024);

  camera_config_t cam = {};
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer   = LEDC_TIMER_0;
  cam.pin_d0=CAM_PIN_D0; cam.pin_d1=CAM_PIN_D1;
  cam.pin_d2=CAM_PIN_D2; cam.pin_d3=CAM_PIN_D3;
  cam.pin_d4=CAM_PIN_D4; cam.pin_d5=CAM_PIN_D5;
  cam.pin_d6=CAM_PIN_D6; cam.pin_d7=CAM_PIN_D7;
  cam.pin_xclk     = CAM_PIN_XCLK;
  cam.pin_pclk     = CAM_PIN_PCLK;
  cam.pin_vsync    = CAM_PIN_VSYNC;
  cam.pin_href     = CAM_PIN_HREF;
  cam.pin_sccb_scl = CAM_PIN_SIOC;
  cam.pin_sccb_sda = CAM_PIN_SIOD;
  cam.pin_pwdn     = CAM_PIN_PWDN;
  cam.pin_reset    = CAM_PIN_RESET;
  cam.xclk_freq_hz = (uint32_t)cfg.xclkMhz * 1000000;
  cam.pixel_format = PIXFORMAT_JPEG;
  cam.jpeg_quality = cfg.jpegQuality;
  cam.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  if (psramFound()) {
    cam.frame_size   = FRAMESIZE_UXGA;
    cam.jpeg_quality = 10;
    cam.fb_count     = 2;
    cam.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    cam.frame_size = FRAMESIZE_SVGA;
    cam.fb_count   = 1;
    cam.grab_mode  = CAMERA_GRAB_LATEST;
  }

  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Init FAILED: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (!s) { Serial.println("[Camera] sensor_get FAILED"); return false; }

  framesize_t target = (cfg.frameSize < FRAMESIZE_MAP_COUNT)
                       ? FRAMESIZE_MAP[cfg.frameSize]
                       : FRAMESIZE_VGA;
  if (target != FRAMESIZE_UXGA) s->set_framesize(s, target);

  int warmupFrames = isReinit ? 3 : 10;
  Serial.printf("[Camera] Warming up (%d frames)", warmupFrames);
  for (int i = 0; i < warmupFrames; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    Serial.print(".");
    delay(isReinit ? 30 : 80);
  }

  // Apply NVS-loaded sensor tuning (brightness, AEC, WB, flips, etc.)
  applySensorSettings();

  // Confirm actual output dimensions
  {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      uint16_t w = 0, h = 0;
      if (jpegSOFDims(fb->buf, fb->len, &w, &h) && w > 0 && h > 0) {
        camWidth = w; camHeight = h;
      } else {
        camWidth = fb->width; camHeight = fb->height;
      }
      Serial.printf("\n[Camera] Ready — %uMHz %s %ux%u\n",
        cfg.xclkMhz, psramFound() ? "PSRAM" : "DRAM", camWidth, camHeight);
      esp_camera_fb_return(fb);
    }
  }

  camReady = true;
  return true;
}

// ---- Buffer functions ----

inline void bufferClear() {
  for (auto& f : frameBuffer)
    if (f.buf) heap_caps_free(f.buf);
  frameBuffer.clear();
  playbackIdx = 0;
}

inline uint32_t bufferClipDurationMs() {
  if (frameBuffer.size() < 2) return 0;
  return frameBuffer.back().captureMs - frameBuffer.front().captureMs;
}

inline bool bufferAppendFrame() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  static size_t   lastLen  = 0;
  static uint32_t lastTail = 0;
  uint32_t thisTail = 0;
  if (fb->len >= 4) memcpy(&thisTail, fb->buf + fb->len - 4, 4);
  if (fb->len == lastLen && thisTail == lastTail) {
    esp_camera_fb_return(fb);
    return false;
  }
  lastLen = fb->len; lastTail = thisTail;
  uint8_t* copy = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy) { memcpy(copy, fb->buf, fb->len); frameBuffer.push_back({ copy, fb->len, millis() }); }
  esp_camera_fb_return(fb);
  return copy != nullptr;
}

inline const FrameEntry* bufferNextFrame() {
  if (frameBuffer.empty()) return nullptr;
  const FrameEntry* f = &frameBuffer[playbackIdx];
  playbackIdx = (playbackIdx + 1) % frameBuffer.size();
  return f;
}

inline bool cameraReinit() {
  camReady = false;
  bufferClear();
  esp_camera_deinit();
  delay(100);
  return cameraInit(true);
}

inline bool cameraOk()           { return camReady; }
inline size_t bufferFrameCount() { return frameBuffer.size(); }
