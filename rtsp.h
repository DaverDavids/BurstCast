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

static const uint16_t FRAME_W[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
static const uint16_t FRAME_H[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

class BurstStreamer : public CStreamer {
public:
  BurstStreamer(u_short w, u_short h) : CStreamer(w, h) {}

  virtual void streamImage(uint32_t curMsec) override {
    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      const FrameEntry* f = bufferNextFrame();
      if (f && f->buf)
        streamFrame(f->buf, f->len, curMsec);
    } else {
      // Live passthrough while idle or recording
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

inline void rtspBegin() {
  uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
  streamer = new BurstStreamer(FRAME_W[fs], FRAME_H[fs]);
  String hostport = WiFi.localIP().toString() + ":554";
  streamer->setURI(hostport, "mjpeg", "1");
  rtspServer.begin();
  Serial.printf("[RTSP] rtsp://%s/mjpeg/1\n", hostport.c_str());
}

inline void rtspHandle() {
  if (!streamer) return;

  // Accept new TCP client
  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    streamer->addSession(heapClient);
    Serial.printf("[RTSP] Client: %s\n",
      heapClient->remoteIP().toString().c_str());
  }

  // Push frame at configured FPS
  if (streamer->anySessions()) {
    static uint32_t lastFrame = 0;
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    if (millis() - lastFrame >= interval) {
      lastFrame = millis();
      streamer->streamImage(millis());
    }
  }

  // Service RTSP control messages (PLAY, TEARDOWN, etc)
  streamer->handleRequests(0);
}
