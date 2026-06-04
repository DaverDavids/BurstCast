#pragma once
// ============================================================
//  rtsp.h — micro-rtsp wrapper (geeksville/Micro-RTSP)
// ============================================================
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "Micro-RTSP/src/CStreamer.h"
#include <esp_camera.h>
#include "camera.h"
#include "config.h"

static uint16_t rtspWidth  = 640;
static uint16_t rtspHeight = 480;

static uint8_t jpegScratch[65536];

static const uint8_t* stripBadMarkers(const uint8_t* src, size_t srcLen, size_t* outLen) {
  uint8_t* dst = jpegScratch;
  size_t dstLen = 0;
  const uint8_t* p = src;
  const uint8_t* end = src + srcLen;

  if (p + 2 > end || p[0] != 0xFF || p[1] != 0xD8) {
    *outLen = srcLen; return src;
  }
  dst[dstLen++] = 0xFF; dst[dstLen++] = 0xD8;
  p += 2;

  while (p + 2 <= end) {
    if (p[0] != 0xFF) {
      size_t rem = end - p;
      if (dstLen + rem <= sizeof(jpegScratch)) { memcpy(dst+dstLen, p, rem); dstLen += rem; }
      break;
    }
    uint8_t marker = p[1]; p += 2;
    if (marker == 0xD8) continue;
    if (marker == 0xD9) { if (dstLen+2<=sizeof(jpegScratch)){dst[dstLen++]=0xFF;dst[dstLen++]=0xD9;} break; }
    if (marker >= 0xD0 && marker <= 0xD7) { if (dstLen+2<=sizeof(jpegScratch)){dst[dstLen++]=0xFF;dst[dstLen++]=marker;} continue; }
    if (p + 2 > end) break;
    uint16_t segLen = (p[0]<<8)|p[1];
    if (segLen < 2) break;
    bool skip = (marker == 0xDD) || (marker >= 0xE0 && marker <= 0xEF);
    if (marker == 0xDA) {
      if (!skip) {
        if (dstLen+2+segLen <= sizeof(jpegScratch)) {
          dst[dstLen++]=0xFF; dst[dstLen++]=0xDA;
          memcpy(dst+dstLen, p, segLen); dstLen += segLen;
        }
      }
      p += segLen;
      size_t rem = end - p;
      if (dstLen + rem <= sizeof(jpegScratch)) { memcpy(dst+dstLen, p, rem); dstLen += rem; }
      break;
    }
    if (!skip) {
      if (dstLen+2+segLen <= sizeof(jpegScratch)) {
        dst[dstLen++]=0xFF; dst[dstLen++]=marker;
        memcpy(dst+dstLen, p, segLen); dstLen += segLen;
      }
    }
    p += segLen;
  }
  *outLen = dstLen;
  return dst;
}

class BurstStreamer : public CStreamer {
public:
  BurstStreamer(u_short w, u_short h)
    : CStreamer(w, h), m_baseCaptureMs(0), m_basePlaybackMs(0), m_videoClockMs(0) {}

  virtual uint8_t getFps() override { return cfg.fps; }

  virtual void streamImage(uint32_t curMsec) override {
    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      const FrameEntry* f = bufferNextFrame();
      if (f && f->buf) {
        // On first frame of a new loop cycle, anchor the playback clock.
        if (m_baseCaptureMs == 0) {
          m_baseCaptureMs  = f->captureMs;
          m_basePlaybackMs = curMsec;
          m_videoClockMs   = curMsec;
        }
        // Drive the RTP timestamp from the original inter-frame capture deltas,
        // not from wall-clock millis(). This gives OBS evenly-spaced frames.
        uint32_t capDelta = f->captureMs - m_baseCaptureMs;
        sendClean(f->buf, f->len, m_basePlaybackMs + capDelta);
      }
    } else {
      // Live passthrough — reset loop anchor and use a synthetic clock that
      // advances by exactly 1000/fps ms per push (no wall-clock drift).
      m_baseCaptureMs = 0;
      uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66UL;
      m_videoClockMs += interval;
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) { sendClean(fb->buf, fb->len, m_videoClockMs); esp_camera_fb_return(fb); }
    }
  }

private:
  uint32_t m_baseCaptureMs;
  uint32_t m_basePlaybackMs;
  uint32_t m_videoClockMs;

  void sendClean(const uint8_t* buf, size_t len, uint32_t ms) {
    size_t cleanLen = 0;
    const uint8_t* clean = stripBadMarkers(buf, len, &cleanLen);
    streamFrame(clean, cleanLen, ms);
  }
};

static WiFiServer     rtspServer(554);
static BurstStreamer* streamer    = nullptr;
// Synthetic pacing clock — advanced per-frame, never reset mid-stream.
static uint32_t       lastFrameMs  = 0;

// Call AFTER cameraInit() so camWidth/camHeight reflect actual sensor output.
inline void rtspBegin(uint16_t w, uint16_t h) {
  if (streamer) {
    delete streamer;
    streamer = nullptr;
  }
  rtspServer.stop();
  delay(50);

  rtspWidth  = w;
  rtspHeight = h;
  Serial.printf("[RTSP] Starting with %ux%u\n", rtspWidth, rtspHeight);
  streamer = new BurstStreamer(rtspWidth, rtspHeight);
  String hostport = WiFi.localIP().toString() + ":554";
  streamer->setURI(hostport, "mjpeg", "1");
  rtspServer.begin();
  Serial.printf("[RTSP] rtsp://%s/mjpeg/1\n", hostport.c_str());
}

inline void rtspHandle() {
  if (!streamer) return;

  // Accept new connections; setNoDelay cuts TCP buffering latency.
  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    heapClient->setNoDelay(true);
    streamer->addSession(heapClient);
    // Reset pacing so the first frame fires immediately on connect.
    lastFrameMs = 0;
    Serial.printf("[RTSP] Client: %s\n", heapClient->remoteIP().toString().c_str());
  }

  // Drain RTSP control messages twice per loop to speed up SETUP/PLAY handshake.
  streamer->handleRequests(0);
  streamer->handleRequests(0);

  if (streamer->anySessions()) {
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66UL;
    if (millis() - lastFrameMs >= interval) {
      lastFrameMs = millis();
      streamer->streamImage(millis());
    }
  }
}
