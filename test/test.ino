// test.ino — dummy random-value sensor for end-to-end OTA/MQTT testing
// All WiFi/MQTT/OTA/NVS logic lives in base.h.
// Only publishSensor() is defined here.
//
// Bump FIRMWARE_VERSION before each test flash so you can confirm
// OTA actually replaced the firmware (check Serial Monitor or
// devices/status after the update completes).

#define SENSOR_NAME "test"

#include "base.h"

const char *FIRMWARE_VERSION = "v1";

void publishSensor(const char *timestamp) {
  float value = random(0, 10000) / 100.0f;  // 0.00 – 99.99

  char payload[200];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"firmware\":\"%s\","
    "\"timestamp\":\"%s\",\"value\":%.2f}",
    deviceIdBuf, SENSOR_NAME, FIRMWARE_VERSION, timestamp, value);
  if (mqttClient.publish("devices/data", payload)) {
    ledDataSent();
  } else {
    ledPublishFail();
  }
  Serial.println(payload);
}

// Override publishStatus to include firmware version in status messages.
// Redefine it here after base.h so the linker picks this one up.
// NOTE: call this only after MQTT is connected (base.h's version guards too).
void publishStatusWithFw(const char *status) {
  if (!mqttClient.connected()) return;
  char payload[180];
  snprintf(payload, sizeof(payload), "%s %s (fw %s)",
    deviceIdBuf, status, FIRMWARE_VERSION);
  mqttClient.publish("devices/status", payload, true);
  Serial.print("Status: "); Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  Serial.print("Firmware version: "); Serial.println(FIRMWARE_VERSION);
  randomSeed(esp_random());
  baseSetup();
  publishStatusWithFw("online");  // re-announce with fw version after connect
}

void loop() {
  baseLoop();

  static uint64_t lastMs = 0;
  uint64_t now = millis();
  if (now - lastMs < 2000) return;
  lastMs = now;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &timeinfo);
  publishSensor(timestamp);
}
