#pragma once
// ============================================================
//  rtsp.h — micro-rtsp wrapper
//  Requires: geeksville/Micro-RTSP (install manually in Arduino)
//  https://github.com/geeksville/Micro-RTSP
// ============================================================
#include <OV2640.h>
#include <OV2640Streamer.h>
#include <CRtspSession.h>
#include <WiFiServer.h>
#include "camera.h"
#include "config.h"

// We drive micro-rtsp with a custom streamer that pulls from our PSRAM buffer
// instead of directly from the camera sensor.

class BurstStreamer : public CStreamer {
public:
  BurstStreamer(SOCKET aClient, int width, int height)
    : CStreamer(aClient, width, height) {}

  virtual void    streamImage(uint32_t curMsec) override {
    // In LOOPING state serve buffer; in IDLE serve live camera frame
    const FrameEntry* f = nullptr;
    camera_fb_t* liveFb = nullptr;

    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      f = bufferNextFrame();
    } else {
      // live passthrough
      liveFb = esp_camera_fb_get();
      if (liveFb) {
        FrameEntry live = { liveFb->buf, liveFb->len };
        f = &live;
      }
    }

    if (f && f->buf) {
      BufPtr start = f->buf;
      BufPtr end   = f->buf + f->len;
      streamJpeg(start, end);
    }

    if (liveFb) esp_camera_fb_return(liveFb);
  }
};

static WiFiServer  rtspServer(554);
static WiFiClient  rtspClient;
static CRtspSession* rtspSession  = nullptr;
static BurstStreamer* rtspStreamer = nullptr;

// Frame dimensions lookup
static const uint16_t FRAME_W[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
static const uint16_t FRAME_H[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

inline void rtspBegin() {
  rtspServer.begin();
  Serial.println("[RTSP] Server listening on :554");
}

inline void rtspHandle() {
  if (rtspSession) {
    // Service existing session
    if (!rtspSession->handleRequests(0)) {
      // client disconnected
      delete rtspSession;  rtspSession  = nullptr;
      delete rtspStreamer; rtspStreamer = nullptr;
      Serial.println("[RTSP] Client disconnected");
    } else {
      uint32_t now = millis();
      if (rtspSession->m_streaming) {
        uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
        // throttle to configured FPS
        static uint32_t lastFrame = 0;
        uint32_t interval = cfg.fps > 0 ? 1000 / cfg.fps : 66;
        if (now - lastFrame >= interval) {
          lastFrame = now;
          rtspStreamer->streamImage(now);
        }
      }
    }
  } else {
    // Accept new client
    rtspClient = rtspServer.accept();
    if (rtspClient) {
      Serial.printf("[RTSP] Client connected: %s\n",
        rtspClient.remoteIP().toString().c_str());
      uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
      rtspStreamer = new BurstStreamer(rtspClient, FRAME_W[fs], FRAME_H[fs]);
      rtspSession  = new CRtspSession(rtspClient, rtspStreamer);
    }
  }
}
