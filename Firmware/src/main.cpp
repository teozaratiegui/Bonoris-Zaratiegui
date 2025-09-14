// ────────────────── Dependencies ──────────────────
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "secrets.h"
#include <BlynkSimpleEsp32.h>
#include "R200.h"
#include "TagGate.h"

// ────────────────── User Config ───────────────────

// Blynk virtual pins (canales lógicos para pasar la información)
#define VPIN_LAST_UID     V0   // último UID (String)
#define VPIN_TAG_PRESENT  V2   // 1 si hay tag, 0 si no

// R200 on Serial2 (17/16 = RX/TX ESP32-WROOM-32)
#define R200_RX_PIN 17
#define R200_TX_PIN 16

// ────────────────── Tuning ────────────────────────
static const uint32_t kPollIntervalMs       = 350;  // R200 poll cadence (si no hay frames)
static const uint32_t kMainLoopIntervalMs   = 60;   // lógica principal
static const uint32_t kTagCooldownMs        = 5000; // ignora mismo UID por este tiempo
static const uint32_t kSendMinIntervalMs    = 150;  // no pushear a Blynk más rápido que esto
#define USE_CONTINUOUS_POLL 1 // si 1, usa modo múltiple (streaming) del R200

// ────────────────── Globals ───────────────────────
BlynkTimer timer;
R200       rfid;

bool          prevTagPresent    = false;
unsigned long lastPushedAt      = 0;
unsigned long lastPollTick      = 0;
unsigned long lastLoopTick      = 0;

TagGate<16> gate(kTagCooldownMs);

uint8_t lastUid[UID_LEN]       = {0};
const  uint8_t zeroUid[UID_LEN] = {0};

// ────────────────── Utils ─────────────────────────
static inline bool sameUid(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, UID_LEN) == 0;
}

static inline void copyUid(uint8_t* dst, const uint8_t* src) {
  memcpy(dst, src, UID_LEN);
}

static inline bool isZeroUid(const uint8_t* uid) {
  return sameUid(uid, zeroUid);
}

static inline String toUidString(const uint8_t* uid) {
  String s;
  for (uint8_t i = 0; i < UID_LEN; i++) {
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// Uptime y presencia (opcionales)
void reportStatusTimer() {
  Blynk.virtualWrite(V1, millis() / 1000);
}

// ────────────────── Setup ─────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting ESP32-WROOM-32 + R200");

  // WiFi + Blynk
  Blynk.begin(BLYNK_TOKEN, WIFI_SSID, WIFI_PASS);
  Blynk.virtualWrite(VPIN_TAG_PRESENT, 0);
  Serial.println(Blynk.connected() ? "Blynk conectado" : "Blynk conectando...");

  // RFID por Serial2 @115200
  rfid.begin(&Serial2, 115200, R200_RX_PIN, R200_TX_PIN);
  Serial2.setRxBufferSize(2048);

  #if USE_CONTINUOUS_POLL
    rfid.setMultiplePollingMode(true); // el R200 streamea inventarios
  #endif

  Serial.println("RFID inicializado");
  rfid.dumpModuleInfo();
  timer.setInterval(1000L, reportStatusTimer);
}

// ────────────────── Loop ──────────────────────────
void loop() {
  Blynk.run();
  timer.run();

  const unsigned long now = millis();

  rfid.loop();

  if ((now - lastPollTick >= kPollIntervalMs) && !rfid.dataAvailable()) {
    rfid.poll();
    lastPollTick = now;
  }

  if (now - lastLoopTick < kMainLoopIntervalMs) return;
  lastLoopTick = now;

  const bool tagPresent   = !isZeroUid(rfid.uid);
  const bool becamePresent = tagPresent && !prevTagPresent;
  const bool becameAbsent = !tagPresent &&  prevTagPresent;

  // ——— Presencia en Blynk: 1 al aparecer, 0 al ausentarse ———
  if (becamePresent) {
    Blynk.virtualWrite(VPIN_TAG_PRESENT, 1);
  }
  if (becameAbsent) {
    Blynk.virtualWrite(VPIN_TAG_PRESENT, 0);
  }

  if (tagPresent) {
    // La caché decide si aceptar (UID nuevo o cooldown vencido)
    if (gate.shouldAccept(rfid.uid, now)) {
      const String uidStr = toUidString(rfid.uid);
      Serial.print("Tag aceptado UID: ");
      Serial.println(uidStr);

      if (now - lastPushedAt >= kSendMinIntervalMs) {
        Blynk.virtualWrite(VPIN_LAST_UID, uidStr);
        lastPushedAt = now;
      }
    }
  }

  prevTagPresent = tagPresent;
}

