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

struct FrameEntry {
  uint8_t* buf;
  size_t   len;
};

static std::vector<FrameEntry> frameBuffer;
static volatile uint16_t       playbackIdx = 0;
static volatile bool           camReady    = false;

// Actual pixel dimensions read from the first good JPEG frame after warmup.
// Set by cameraInit(); used by rtspBegin() to match the SDP to reality.
uint16_t camWidth  = 640;
uint16_t camHeight = 480;

// Parse width/height out of a JPEG SOF0/SOF2 marker.
// Returns true and fills w/h if found.
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

inline void debugJpegFrame(const uint8_t* buf, size_t len, const char* tag) {
  Serial.printf("[JPEG/%s] len=%u first4: %02X %02X %02X %02X\n",
    tag, (unsigned)len,
    len>0?buf[0]:0, len>1?buf[1]:0, len>2?buf[2]:0, len>3?buf[3]:0);
  uint16_t w=0, h=0;
  if (jpegSOFDims(buf, len, &w, &h))
    Serial.printf("[JPEG/%s] SOF: %ux%u\n", tag, w, h);
  else
    Serial.printf("[JPEG/%s] WARNING: SOF not found\n", tag);
}

inline bool cameraInit() {
  if (!psramFound())
    Serial.println("[Camera] PSRAM not found!");
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
  cam.pin_sccb_sda = CAM_PIN_SIOD;
  cam.pin_sccb_scl = CAM_PIN_SIOC;
  cam.pin_pwdn     = CAM_PIN_PWDN;
  cam.pin_reset    = CAM_PIN_RESET;
  cam.xclk_freq_hz = (uint32_t)cfg.xclkMhz * 1000000;
  cam.pixel_format = PIXFORMAT_JPEG;
  cam.frame_size   = FRAMESIZE_QVGA;
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

  // Flush warmup frames so AE/AWB locks and framesize settles.
  // Parse the last frame's SOF to get the TRUE pixel dimensions
  // the sensor actually produced — this is what we tell Micro-RTSP.
  Serial.print("[Camera] Warming up");
  for (int i = 0; i < 8; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (i == 7) {
        uint16_t w = 0, h = 0;
        if (jpegSOFDims(fb->buf, fb->len, &w, &h) && w > 0 && h > 0) {
          camWidth  = w;
          camHeight = h;
        }
        Serial.printf(" done. %ux%u (%u bytes)\n", camWidth, camHeight, fb->len);
      }
      esp_camera_fb_return(fb);
    }
    delay(80);
  }

  Serial.printf("[Camera] OK — %dMHz PSRAM=%s frameSize=%u\n",
    cfg.xclkMhz, psramFound() ? "yes" : "NO", cfg.frameSize);
  camReady = true;
  return true;
}

inline void bufferClear() {
  for (auto& f : frameBuffer)
    if (f.buf) heap_caps_free(f.buf);
  frameBuffer.clear();
  playbackIdx = 0;
}

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

inline const FrameEntry* bufferNextFrame() {
  if (frameBuffer.empty()) return nullptr;
  const FrameEntry* f = &frameBuffer[playbackIdx];
  playbackIdx = (playbackIdx + 1) % frameBuffer.size();
  return f;
}

inline bool cameraOk()          { return camReady; }
inline size_t bufferFrameCount(){ return frameBuffer.size(); }
