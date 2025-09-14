#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

// -------- Runtime transport selection --------
enum class TransportMode : uint8_t { HTTP = 0, MQTT = 1 };

struct MessageGatewayConfig {
  TransportMode mode = TransportMode::HTTP;

  // HTTP
  String httpUrl;                 // e.g. "http://192.168.1.10:8080/ingest"
  String httpAuthBearer;          // optional: "token"

  // MQTT
  String  mqttHost = "192.168.1.5";
  uint16_t mqttPort = 1883;
  String  mqttTopic = "r200/ingest";
  String  mqttUser;               // optional
  String  mqttPass;               // optional
  String  mqttClientId = "esp32-r200";
  bool    mqttRetain = true;      // retain last message by default

  // Timestamp source: millis() by default. If you want epoch ms, set a provider.
  uint32_t (*timeProviderMs)() = nullptr; // if null, uses millis()
};

class MessageGateway {
public:
  explicit MessageGateway(const MessageGatewayConfig& cfg)
  : cfg_(cfg), mqtt_(net_) {}

  void begin() {
    if (cfg_.mode == TransportMode::MQTT) {
      mqtt_.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
      ensureMqtt();
    }
  }

  void setEnabled(bool e) { enabled_ = e; }
  bool isEnabled() const { return enabled_; }

  void setMode(TransportMode m) {
    cfg_.mode = m;
    if (cfg_.mode == TransportMode::MQTT) {
      mqtt_.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
      ensureMqtt();
    }
  }

  // Main API: send UID + timestamp (ms). Returns true on success.
  bool sendTag(const String& uid) {
    if (!enabled_) return false;
    const uint32_t ts = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
    const String payload = buildPayload(uid, ts);

    if (cfg_.mode == TransportMode::HTTP) return sendHttp(payload);
    return sendMqtt(payload);
  }

  // Optional: notify absence if you want (tag_present:false)
  bool sendAbsent() {
    if (!enabled_) return false;
    const uint32_t ts = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
    const String payload = buildPayload("", ts, /*present=*/false);
    if (cfg_.mode == TransportMode::HTTP) return sendHttp(payload);
    return sendMqtt(payload);
  }

private:
  MessageGatewayConfig cfg_;
  bool enabled_ = true;

  WiFiClient   net_;
  PubSubClient mqtt_;

  // ---- JSON builder (edit here if you want a different schema) ----
  static String buildPayload(const String& uid, uint32_t tsMs, bool present = true) {
    // { "uid":"ABCD...", "ts": 1234567, "tag_present": true/false }
    String p = "{\"ts\":";
    p += String(tsMs);
    p += ",\"tag_present\":";
    p += present ? "true" : "false";
    if (present) {
      p += ",\"uid\":\"";
      p += uid;
      p += "\"";
    }
    p += "}";
    return p;
  }

  // ---- HTTP ----
  bool sendHttp(const String& payload) {
    if (cfg_.httpUrl.length() == 0) return false;
    HTTPClient http;
    http.begin(cfg_.httpUrl);
    http.addHeader("Content-Type", "application/json");
    if (cfg_.httpAuthBearer.length())
      http.addHeader("Authorization", "Bearer " + cfg_.httpAuthBearer);
    const int code = http.POST(payload);
    http.end();
    return (code > 0 && code < 400);
  }

  // ---- MQTT ----
  bool ensureMqtt() {
    if (mqtt_.connected()) return true;
    for (uint8_t i = 0; i < 3 && !mqtt_.connected(); ++i) {
      String cid = cfg_.mqttClientId + "-" + String((uint32_t)esp_random(), HEX);
      if (cfg_.mqttUser.length())
        mqtt_.connect(cid.c_str(), cfg_.mqttUser.c_str(), cfg_.mqttPass.c_str());
      else
        mqtt_.connect(cid.c_str());
      delay(100);
    }
    return mqtt_.connected();
  }

  bool sendMqtt(const String& payload) {
    if (!ensureMqtt()) return false;
    return mqtt_.publish(cfg_.mqttTopic.c_str(), payload.c_str(), cfg_.mqttRetain);
  }
};
