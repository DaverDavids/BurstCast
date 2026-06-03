#pragma once
// ============================================================
//  rtsp.h — micro-rtsp wrapper (geeksville/Micro-RTSP)
//
//  Problem: CStreamer::streamFrame() calls decodeJPEGfile() internally.
//  decodeJPEGfile()/findJPEGheader() does not handle:
//    0xdd DRI (restart interval) — present in OV3660 output
//    0xe1 APP1 (EXIF)            — sometimes present
//  When it hits these in the default: case it silently corrupts the
//  scan pointer, causing the green-frame / double-image artifact.
//
//  Fix: subclass CStreamer, override streamFrame to do our own
//  JPEG scan that skips all APP/DRI markers before handing off
//  to SendRtpPacket... but SendRtpPacket is private.
//
//  Simpler fix: pre-process the buffer to strip the JFIF wrapper
//  ourselves with a robust parser, then call the parent streamFrame
//  with a buffer that starts exactly at the SOS scan data with
//  valid Q-table pointers — but that replicates the whole decoder.
//
//  Simplest correct fix: use OV2640Streamer as reference — it calls
//  streamFrame with the raw fb->buf. The issue is decodeJPEGfile
//  can't handle DRI. We patch around it by stripping known-bad
//  markers from the JPEG before passing to streamFrame.
// ============================================================
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <CStreamer.h>
#include <esp_camera.h>
#include "camera.h"
#include "config.h"

static uint16_t rtspWidth  = 640;
static uint16_t rtspHeight = 480;

// Strip DRI (0xdd) and any APP (0xe0-0xef) markers from a JPEG buffer
// into a static scratch buffer so streamFrame's decodeJPEGfile succeeds.
// Returns pointer to cleaned buffer and updates len.
static uint8_t jpegScratch[65536];  // fits up to SVGA in PSRAM-less scenario; PSRAM users can increase

static const uint8_t* stripBadMarkers(const uint8_t* src, size_t srcLen, size_t* outLen) {
  uint8_t* dst = jpegScratch;
  size_t dstLen = 0;
  const uint8_t* p = src;
  const uint8_t* end = src + srcLen;

  // Copy SOI (FF D8)
  if (p + 2 > end || p[0] != 0xFF || p[1] != 0xD8) {
    *outLen = srcLen; return src; // not JPEG, pass through
  }
  dst[dstLen++] = 0xFF;
  dst[dstLen++] = 0xD8;
  p += 2;

  while (p + 2 <= end) {
    if (p[0] != 0xFF) { // sync lost, copy rest verbatim
      size_t rem = end - p;
      if (dstLen + rem <= sizeof(jpegScratch)) {
        memcpy(dst + dstLen, p, rem);
        dstLen += rem;
      }
      break;
    }
    uint8_t marker = p[1];
    p += 2;

    // Markers with no length field
    if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) {
      if (marker == 0xD9) { // EOI — copy and stop
        if (dstLen + 2 <= sizeof(jpegScratch)) { dst[dstLen++]=0xFF; dst[dstLen++]=0xD9; }
        break;
      }
      // RST markers — copy through
      if (dstLen + 2 <= sizeof(jpegScratch)) { dst[dstLen++]=0xFF; dst[dstLen++]=marker; }
      continue;
    }

    if (p + 2 > end) break;
    uint16_t segLen = (p[0] << 8) | p[1]; // includes the 2 length bytes

    bool skip = false;
    // Skip DRI (DD) and all APPn (E0-EF) except keep nothing — DQT/DHT/SOF/SOS are kept
    if (marker == 0xDD) skip = true;            // DRI restart interval
    if (marker >= 0xE0 && marker <= 0xEF) skip = true; // APPn

    if (!skip) {
      // Copy marker + segment
      size_t total = 2 + segLen;
      if (dstLen + total <= sizeof(jpegScratch)) {
        dst[dstLen++] = 0xFF;
        dst[dstLen++] = marker;
        memcpy(dst + dstLen, p, segLen);
        dstLen += segLen;
      }
      // If this is SOS, copy the rest of the file verbatim (scan data)
      if (marker == 0xDA) {
        p += segLen;
        size_t rem = end - p;
        if (dstLen + rem <= sizeof(jpegScratch)) {
          memcpy(dst + dstLen, p, rem);
          dstLen += rem;
        }
        break;
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
static BurstStreamer* streamer = nullptr;

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

  WiFiClient newClient = rtspServer.accept();
  if (newClient) {
    WiFiClient* heapClient = new WiFiClient(newClient);
    streamer->addSession(heapClient);
    Serial.printf("[RTSP] Client: %s\n", heapClient->remoteIP().toString().c_str());
  }

  if (streamer->anySessions()) {
    streamer->handleRequests(0);
    static uint32_t lastFrame = 0;
    uint32_t interval = cfg.fps > 0 ? 1000UL / cfg.fps : 66;
    if (millis() - lastFrame >= interval) {
      lastFrame = millis();
      streamer->streamImage(millis());
    }
  }
}
