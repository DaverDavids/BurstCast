#pragma once
// ============================================================
//  rtsp.h — micro-rtsp wrapper (geeksville/Micro-RTSP)
// ============================================================
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <CStreamer.h>
#include <esp_camera.h>
#include "camera.h"
#include "config.h"

// Actual frame dimensions read from sensor after init
static uint16_t rtspWidth  = 640;
static uint16_t rtspHeight = 480;

class BurstStreamer : public CStreamer {
public:
  BurstStreamer(u_short w, u_short h) : CStreamer(w, h) {}

  virtual void streamImage(uint32_t curMsec) override {
    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      const FrameEntry* f = bufferNextFrame();
      if (f && f->buf)
        streamFrame(f->buf, f->len, curMsec);
    } else {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        streamFrame(fb->buf, fb->len, curMsec);
        esp_camera_fb_return(fb);
      }
    }
  }
};

static WiFiServer     rtspServer(554);
static BurstStreamer* streamer = nullptr;

// Call AFTER cameraInit() so sensor dimensions are known
inline void rtspBegin() {
  // Read actual frame size from sensor rather than config table
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // esp_camera framesize_t -> resolution via status
    camera_status_t* st = &s->status;
    // Map framesize enum to pixel dims
    static const uint16_t FW[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
    static const uint16_t FH[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};
    uint8_t fs = st->framesize < 13 ? st->framesize : 6;
    rtspWidth  = FW[fs];
    rtspHeight = FH[fs];
  }
  Serial.printf("[RTSP] Frame size: %dx%d\n", rtspWidth, rtspHeight);

  streamer = new BurstStreamer(rtspWidth, rtspHeight);
  String hostport = WiFi.localIP().toString() + ":554";
  streamer->setURI(hostport, "mjpeg", "1");
  rtspServer.begin();
  Serial.printf("[RTSP] rtsp://%s/mjpeg/1\n", hostport.c_str());
}

inline void rtspHandle() {
  if (!streamer) return;

  // Accept new client
  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    streamer->addSession(heapClient);
    Serial.printf("[RTSP] Client: %s\n",
      heapClient->remoteIP().toString().c_str());
  }

  if (streamer->anySessions()) {
    // Service RTSP control (PLAY, TEARDOWN, etc)
    streamer->handleRequests(0);

    // Push frame at configured FPS
    static uint32_t lastFrame = 0;
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    if (millis() - lastFrame >= interval) {
      lastFrame = millis();
      streamer->streamImage(millis());
    }
  }
}
