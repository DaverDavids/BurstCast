#pragma once
// ============================================================
//  rtsp.h — micro-rtsp wrapper (geeksville/Micro-RTSP)
//
//  Actual API (from CStreamer.h):
//    CStreamer(u_short width, u_short height)
//    CRtspSession* addSession(SOCKET)   where SOCKET = WiFiClient*
//    bool handleRequests(uint32_t timeoutMs)
//    virtual void streamImage(uint32_t curMsec) = 0
//    void setURI(String hostport, String pres, String stream)
// ============================================================
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <CStreamer.h>
#include <esp_camera.h>
#include "camera.h"
#include "config.h"

static const uint16_t FRAME_W[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
static const uint16_t FRAME_H[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};

// ---- Custom streamer: pulls from PSRAM buffer or live camera ----
class BurstStreamer : public CStreamer {
public:
  BurstStreamer(u_short w, u_short h) : CStreamer(w, h) {}

  virtual void streamImage(uint32_t curMsec) override {
    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      // Serve looping PSRAM buffer
      const FrameEntry* f = bufferNextFrame();
      if (f && f->buf) {
        BufPtr start = f->buf;
        BufPtr end   = f->buf + f->len;
        streamJpeg(start, end, curMsec);
      }
    } else {
      // Live passthrough
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        BufPtr start = fb->buf;
        BufPtr end   = fb->buf + fb->len;
        streamJpeg(start, end, curMsec);
        esp_camera_fb_return(fb);
      }
    }
  }

private:
  // Helper: decode JPEG and send via CStreamer::streamFrame
  void streamJpeg(BufPtr start, BufPtr end, uint32_t curMsec) {
    BufPtr qtable0 = nullptr, qtable1 = nullptr;
    uint32_t len = end - start;
    // decodeJPEGfile strips the JFIF wrapper and extracts Q-tables
    if (decodeJPEGfile(&start, &len, &qtable0, &qtable1)) {
      streamFrame(start, len, curMsec);
    }
  }
};

static WiFiServer    rtspServer(554);
static BurstStreamer* streamer = nullptr;

inline void rtspBegin() {
  uint8_t fs = cfg.frameSize < 13 ? cfg.frameSize : 6;
  streamer = new BurstStreamer(FRAME_W[fs], FRAME_H[fs]);
  // Tell clients what URI to use: rtsp://<ip>:554/mjpeg/1
  String hostport = WiFi.localIP().toString() + ":554";
  streamer->setURI(hostport, "mjpeg", "1");
  rtspServer.begin();
  Serial.printf("[RTSP] Listening on rtsp://%s/mjpeg/1\n", hostport.c_str());
}

inline void rtspHandle() {
  if (!streamer) return;

  // Accept new TCP connections and hand them to the streamer
  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    streamer->addSession(heapClient);
    Serial.printf("[RTSP] Client connected: %s\n",
      heapClient->remoteIP().toString().c_str());
  }

  // Service all sessions (RTSP handshake + RTP send)
  if (streamer->anySessions()) {
    static uint32_t lastFrame = 0;
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    if (millis() - lastFrame >= interval) {
      lastFrame = millis();
      streamer->streamImage(millis());
    }
    streamer->handleRequests(0);
  }
}
