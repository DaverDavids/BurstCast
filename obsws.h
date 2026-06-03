#pragma once
// ============================================================
//  obsws.h — OBS WebSocket 5.x client
//  Requires: ArduinoWebsockets + ArduinoJson (Library Manager)
//  Handles: connect, SHA256 auth, GetSceneItemId, SetSceneItemEnabled
// ============================================================
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "config.h"

using namespace websockets;

static WebsocketsClient obsWs;
static bool   obsWsConnected   = false;
static int32_t obsSceneItemId  = -1;  // resolved at connect time
static uint32_t obsReqId       = 1;

// ---- SHA256 base64 helper for OBS auth ----
static String sha256b64(const String& input) {
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t*)input.c_str(), input.length(), hash, 0);
  size_t outLen = 0;
  uint8_t encoded[64];
  mbedtls_base64_encode(encoded, sizeof(encoded), &outLen,
                        hash, sizeof(hash));
  return String((char*)encoded).substring(0, outLen);
}

static String obsReqIdStr() {
  return String(obsReqId++);
}

// Send a raw OBS WS op-6 request
static void obsSend(const char* type, JsonObject& data) {
  StaticJsonDocument<512> doc;
  doc["op"] = 6;
  JsonObject d = doc.createNestedObject("d");
  d["requestType"] = type;
  d["requestId"]   = obsReqIdStr();
  d["requestData"] = data;
  String out;
  serializeJson(doc, out);
  obsWs.send(out);
}

// Ask OBS for the numeric scene item ID of our source
static void obsGetSceneItemId() {
  StaticJsonDocument<256> doc;
  JsonObject data = doc.to<JsonObject>();
  data["sceneName"]  = cfg.obsSceneName;
  data["sourceName"] = cfg.obsSourceName;
  StaticJsonDocument<512> req;
  req["op"] = 6;
  JsonObject d = req.createNestedObject("d");
  d["requestType"] = "GetSceneItemId";
  d["requestId"]   = "getid";
  d["requestData"] = data;
  String out;
  serializeJson(req, out);
  obsWs.send(out);
}

static void obsSetSourceVisible(bool visible) {
  if (!obsWsConnected || obsSceneItemId < 0) return;
  StaticJsonDocument<256> dataDoc;
  JsonObject data = dataDoc.to<JsonObject>();
  data["sceneName"]        = cfg.obsSceneName;
  data["sceneItemId"]      = obsSceneItemId;
  data["sceneItemEnabled"] = visible;
  obsSend("SetSceneItemEnabled", data);
  Serial.printf("[OBS] Source '%s' -> %s\n",
    cfg.obsSourceName, visible ? "VISIBLE" : "HIDDEN");
}

static void obsOnMessage(WebsocketsMessage msg) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, msg.data()) != DeserializationError::Ok) return;
  int op = doc["op"] | -1;

  if (op == 2) {
    // Hello — check if auth required
    JsonObject d = doc["d"];
    if (d.containsKey("authentication")) {
      String challenge = d["authentication"]["challenge"].as<String>();
      String salt      = d["authentication"]["salt"].as<String>();
      // OBS auth: base64(sha256(base64(sha256(password + salt)) + challenge))
      String secret    = sha256b64(String(cfg.obsWsPass) + salt);
      String authResp  = sha256b64(secret + challenge);
      StaticJsonDocument<256> auth;
      auth["op"] = 1;
      JsonObject ad = auth.createNestedObject("d");
      ad["rpcVersion"]     = 1;
      ad["authentication"] = authResp;
      String out; serializeJson(auth, out);
      obsWs.send(out);
    } else {
      // No auth needed
      StaticJsonDocument<64> ident;
      ident["op"] = 1;
      ident.createNestedObject("d")["rpcVersion"] = 1;
      String out; serializeJson(ident, out);
      obsWs.send(out);
    }
  }
  else if (op == 5) {
    // Identified — connection ready, resolve scene item ID
    Serial.println("[OBS] WebSocket authenticated");
    obsWsConnected = true;
    if (cfg.obsSceneName[0] && cfg.obsSourceName[0]) obsGetSceneItemId();
  }
  else if (op == 7) {
    // RequestResponse
    const char* reqId = doc["d"]["requestId"] | "";
    if (strcmp(reqId, "getid") == 0) {
      int32_t id = doc["d"]["responseData"]["sceneItemId"] | -1;
      if (id >= 0) {
        obsSceneItemId = id;
        Serial.printf("[OBS] Scene item ID for '%s': %d\n",
          cfg.obsSourceName, obsSceneItemId);
      } else {
        Serial.println("[OBS] WARNING: source not found in scene — check names");
      }
    }
  }
}

inline void obsWsBegin() {
  if (cfg.obsWsIp[0] == '\0') return;
  obsWs.onMessage(obsOnMessage);
  obsWs.onEvent([](WebsocketsEvent ev, String data) {
    if (ev == WebsocketsEvent::ConnectionOpened) {
      Serial.println("[OBS] WS connected");
    } else if (ev == WebsocketsEvent::ConnectionClosed) {
      Serial.println("[OBS] WS disconnected");
      obsWsConnected = false;
      obsSceneItemId = -1;
    }
  });
  String url = String("ws://") + cfg.obsWsIp + ":" + String(cfg.obsWsPort);
  obsWs.connect(url);
  Serial.printf("[OBS] Connecting to %s\n", url.c_str());
}

inline void obsWsHandle() {
  obsWs.poll();
  // Reconnect if config set but disconnected
  static uint32_t lastTry = 0;
  if (!obsWsConnected && cfg.obsWsIp[0] && millis() - lastTry > 10000) {
    lastTry = millis();
    obsWsBegin();
  }
}

inline bool obsWsReady() { return obsWsConnected && obsSceneItemId >= 0; }
