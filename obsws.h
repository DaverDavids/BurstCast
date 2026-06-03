#pragma once
// ============================================================
//  obsws.h — OBS WebSocket 5.x client
//  OBS WS5 op codes:
//    0 = Hello        (server -> client)
//    1 = Identify     (client -> server)
//    2 = Identified   (server -> client, auth success)
//    6 = Request      (client -> server)
//    7 = RequestResponse (server -> client)
// ============================================================
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "config.h"

using namespace websockets;

static WebsocketsClient obsWs;
static bool    obsWsConnected  = false;
static int32_t obsSceneItemId  = -1;
static uint32_t obsReqId       = 1;
static bool    obsWsEverStarted = false;

static String sha256b64(const String& input) {
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t*)input.c_str(), input.length(), hash, 0);
  size_t outLen = 0;
  uint8_t encoded[64];
  mbedtls_base64_encode(encoded, sizeof(encoded), &outLen, hash, sizeof(hash));
  return String((char*)encoded).substring(0, outLen);
}

static String obsReqIdStr() { return String(obsReqId++); }

static void obsSendRaw(const String& json) {
  if (obsWs.available()) obsWs.send(json);
}

static void obsGetSceneItemId() {
  StaticJsonDocument<512> req;
  req["op"] = 6;
  JsonObject d = req.createNestedObject("d");
  d["requestType"] = "GetSceneItemId";
  d["requestId"]   = "getid";
  JsonObject rd = d.createNestedObject("requestData");
  rd["sceneName"]  = cfg.obsSceneName;
  rd["sourceName"] = cfg.obsSourceName;
  String out; serializeJson(req, out);
  obsSendRaw(out);
}

static void obsSetSourceVisible(bool visible) {
  if (!obsWs.available() || obsSceneItemId < 0) return;
  StaticJsonDocument<512> req;
  req["op"] = 6;
  JsonObject d = req.createNestedObject("d");
  d["requestType"] = "SetSceneItemEnabled";
  d["requestId"]   = obsReqIdStr();
  JsonObject rd = d.createNestedObject("requestData");
  rd["sceneName"]        = cfg.obsSceneName;
  rd["sceneItemId"]      = obsSceneItemId;
  rd["sceneItemEnabled"] = visible;
  String out; serializeJson(req, out);
  obsSendRaw(out);
  Serial.printf("[OBS] '%s' -> %s\n", cfg.obsSourceName, visible ? "SHOW" : "HIDE");
}

static void obsOnMessage(WebsocketsMessage msg) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, msg.data()) != DeserializationError::Ok) return;
  int op = doc["op"] | -1;
  Serial.printf("[OBS] op=%d\n", op);  // debug: remove once auth is confirmed working

  if (op == 0) {
    // Hello — send Identify with optional auth
    JsonObject d = doc["d"];
    StaticJsonDocument<256> ident;
    ident["op"] = 1;
    JsonObject id = ident.createNestedObject("d");
    id["rpcVersion"] = 1;
    if (d.containsKey("authentication")) {
      String challenge = d["authentication"]["challenge"].as<String>();
      String salt      = d["authentication"]["salt"].as<String>();
      String secret    = sha256b64(String(cfg.obsWsPass) + salt);
      id["authentication"] = sha256b64(secret + challenge);
    }
    String out; serializeJson(ident, out);
    obsSendRaw(out);
    Serial.println("[OBS] Sent Identify");
  }
  else if (op == 2) {
    // Identified — auth accepted
    Serial.println("[OBS] Authenticated");
    obsWsConnected = true;
    if (cfg.obsSceneName[0] && cfg.obsSourceName[0]) obsGetSceneItemId();
  }
  else if (op == 7) {
    const char* reqId = doc["d"]["requestId"] | "";
    bool ok = doc["d"]["requestStatus"]["result"] | false;
    if (strcmp(reqId, "getid") == 0) {
      if (ok) {
        int32_t id = doc["d"]["responseData"]["sceneItemId"] | -1;
        obsSceneItemId = id;
        Serial.printf("[OBS] Scene item ID '%s': %d\n", cfg.obsSourceName, id);
      } else {
        int code = doc["d"]["requestStatus"]["code"] | 0;
        Serial.printf("[OBS] GetSceneItemId failed (code %d) — check Scene/Source names\n", code);
      }
    }
  }
}

inline void obsWsBegin() {
  if (cfg.obsWsIp[0] == '\0') return;
  if (!obsWsEverStarted) {
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
    obsWsEverStarted = true;
  }
  String url = String("ws://") + cfg.obsWsIp + ":" + String(cfg.obsWsPort);
  Serial.printf("[OBS] Connecting to %s\n", url.c_str());
  obsWs.connect(url);
}

inline void obsWsHandle() {
  if (!obsWsEverStarted) return;
  obsWs.poll();
  static uint32_t lastTry = 0;
  if (!obsWs.available() && cfg.obsWsIp[0] && millis() - lastTry > 10000) {
    lastTry = millis();
    obsWsBegin();
  }
}

inline bool obsWsReady() { return obsWs.available() && obsSceneItemId >= 0; }
