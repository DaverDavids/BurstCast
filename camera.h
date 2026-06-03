#pragma once
// ============================================================
//  camera.h — PSRAM frame buffer, capture, loop playback
// ============================================================
#include <esp_camera.h>
#include <vector>
#include "config.h"

// Pin map — Freenove ESP32-S3-WROOM N16R8 + OV3660
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

struct FrameEntry {
  uint8_t* buf;
  size_t   len;
};

// PSRAM frame store
static std::vector<FrameEntry> frameBuffer;
static volatile uint16_t       playbackIdx  = 0;
static volatile bool           camReady     = false;

inline bool cameraInit() {
  if (!psramFound()) {
    Serial.println("[Camera] PSRAM not found! Check board PSRAM setting = OPI PSRAM");
  } else {
    Serial.printf("[Camera] PSRAM free: %u KB\n", ESP.getFreePsram() / 1024);
  }

  camera_config_t cam;
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer   = LEDC_TIMER_0;
  cam.pin_d0 = CAM_PIN_D0; cam.pin_d1 = CAM_PIN_D1;
  cam.pin_d2 = CAM_PIN_D2; cam.pin_d3 = CAM_PIN_D3;
  cam.pin_d4 = CAM_PIN_D4; cam.pin_d5 = CAM_PIN_D5;
  cam.pin_d6 = CAM_PIN_D6; cam.pin_d7 = CAM_PIN_D7;
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
  cam.frame_size   = FRAMESIZE_QVGA;  // init small, resize after
  cam.jpeg_quality = cfg.jpegQuality;
  cam.fb_count     = psramFound() ? 2 : 1;
  cam.grab_mode    = CAMERA_GRAB_LATEST;
  cam.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Init FAILED: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("[Camera] Sensor PID: 0x%x\n", s->id.PID);
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_framesize(s, (framesize_t)cfg.frameSize);
  }
  Serial.printf("[Camera] OK — %dMHz PSRAM=%s\n",
    cfg.xclkMhz, psramFound() ? "yes" : "NO");
  camReady = true;
  return true;
}

// Free old buffer and allocate fresh from PSRAM
inline void bufferClear() {
  for (auto& f : frameBuffer) {
    if (f.buf) heap_caps_free(f.buf);
  }
  frameBuffer.clear();
  playbackIdx = 0;
}

// Copy one camera frame into PSRAM-backed buffer
inline bool bufferAppendFrame() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  uint8_t* copy = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy) {
    memcpy(copy, fb->buf, fb->len);
    frameBuffer.push_back({ copy, fb->len });
  }
  esp_camera_fb_return(fb);
  return copy != nullptr;
}

// Returns the next frame in the loop (wraps)
inline const FrameEntry* bufferNextFrame() {
  if (frameBuffer.empty()) return nullptr;
  const FrameEntry* f = &frameBuffer[playbackIdx];
  playbackIdx = (playbackIdx + 1) % frameBuffer.size();
  return f;
}

inline bool cameraOk() { return camReady; }
inline size_t bufferFrameCount() { return frameBuffer.size(); }
