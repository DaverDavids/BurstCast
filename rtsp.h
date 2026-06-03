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

static uint16_t rtspWidth  = 640;
static uint16_t rtspHeight = 480;

// Strip DRI (0xdd) and all APPn (0xe0-0xef) markers from a JPEG buffer.
// Micro-RTSP's internal JPEG parser chokes on these markers.
static uint8_t jpegScratch[65536];

static const uint8_t* stripBadMarkers(const uint8_t* src, size_t srcLen, size_t* outLen) {
  uint8_t* dst = jpegScratch;
  size_t dstLen = 0;
  const uint8_t* p = src;
  const uint8_t* end = src + srcLen;

  if (p + 2 > end || p[0] != 0xFF || p[1] != 0xD8) {
    *outLen = srcLen; return src;
  }
  dst[dstLen++] = 0xFF;
  dst[dstLen++] = 0xD8;
  p += 2;

  while (p + 2 <= end) {
    if (p[0] != 0xFF) {
      // Not a marker — raw data (shouldn't happen outside SOS, copy verbatim)
      size_t rem = end - p;
      if (dstLen + rem <= sizeof(jpegScratch)) { memcpy(dst + dstLen, p, rem); dstLen += rem; }
      break;
    }
    uint8_t marker = p[1];
    p += 2;  // skip FF XX

    // Standalone markers (no length field)
    if (marker == 0xD8) continue;  // SOI
    if (marker == 0xD9) {          // EOI
      if (dstLen + 2 <= sizeof(jpegScratch)) { dst[dstLen++]=0xFF; dst[dstLen++]=0xD9; }
      break;
    }
    if (marker >= 0xD0 && marker <= 0xD7) { // RST0-RST7
      if (dstLen + 2 <= sizeof(jpegScratch)) { dst[dstLen++]=0xFF; dst[dstLen++]=marker; }
      continue;
    }

    // All remaining markers have a 2-byte big-endian length that includes itself
    if (p + 2 > end) break;
    uint16_t segLen = (p[0] << 8) | p[1];  // includes the 2 length bytes
    if (segLen < 2) break;                  // malformed

    bool skip = (marker == 0xDD) || (marker >= 0xE0 && marker <= 0xEF);

    if (marker == 0xDA) {
      // SOS: keep the header, then copy the rest of the file verbatim
      // (entropy-coded data has no length field; runs to next marker/EOI)
      if (!skip) {
        size_t headerTotal = 2 + segLen;  // FF DA + length field + parameters
        if (dstLen + headerTotal <= sizeof(jpegScratch)) {
          dst[dstLen++] = 0xFF;
          dst[dstLen++] = 0xDA;
          memcpy(dst + dstLen, p, segLen);  // p still points at length bytes
          dstLen += segLen;
        }
      }
      // p + segLen = first byte of entropy data
      p += segLen;
      size_t rem = end - p;
      if (dstLen + rem <= sizeof(jpegScratch)) { memcpy(dst + dstLen, p, rem); dstLen += rem; }
      break;  // nothing after entropy data except EOI which we already include
    }

    if (!skip) {
      size_t total = 2 + segLen;
      if (dstLen + total <= sizeof(jpegScratch)) {
        dst[dstLen++] = 0xFF;
        dst[dstLen++] = marker;
        memcpy(dst + dstLen, p, segLen);
        dstLen += segLen;
      }
    }
    p += segLen;
  }

  *outLen = dstLen;
  return dst;
}

class BurstStreamer : public CStreamer {
public:
  BurstStreamer(u_short w, u_short h) : CStreamer(w, h) {}

  virtual void streamImage(uint32_t curMsec) override {
    if (burstState == STATE_LOOPING && bufferFrameCount() > 0) {
      const FrameEntry* f = bufferNextFrame();
      if (f && f->buf) sendClean(f->buf, f->len, curMsec);
    } else {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        sendClean(fb->buf, fb->len, curMsec);
        esp_camera_fb_return(fb);
      }
    }
  }

private:
  void sendClean(const uint8_t* buf, size_t len, uint32_t ms) {
    size_t cleanLen = 0;
    const uint8_t* clean = stripBadMarkers(buf, len, &cleanLen);
    streamFrame(clean, cleanLen, ms);
  }
};

static WiFiServer     rtspServer(554);
static BurstStreamer* streamer   = nullptr;
static uint32_t       lastFrameMs = 0;

inline void rtspBegin() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    static const uint16_t FW[] = {160,176,240,240,320,400,480,640,800,1024,1280,1280,1600};
    static const uint16_t FH[] = {120,144,176,240,240,296,320,480,600, 768, 720,1024,1200};
    uint8_t fs = s->status.framesize < 13 ? s->status.framesize : 6;
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

  // Accept new client — reset frame timer so first frame fires immediately
  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    heapClient->setNoDelay(true);  // disable Nagle for RTP fragments
    streamer->addSession(heapClient);
    lastFrameMs = 0;  // fire first frame on next tick
    Serial.printf("[RTSP] Client: %s\n", heapClient->remoteIP().toString().c_str());
  }

  // Run RTSP command handling every tick — OPTIONS/DESCRIBE/SETUP/PLAY
  // must complete at full loop speed, not gated on anySessions().
  streamer->handleRequests(0);

  if (streamer->anySessions()) {
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    if (millis() - lastFrameMs >= interval) {
      lastFrameMs = millis();
      streamer->streamImage(millis());
    }
  }
}
