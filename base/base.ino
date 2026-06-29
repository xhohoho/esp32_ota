// base.ino — WiFi/MQTT/OTA baseline with no sensor payload
// All connection logic lives in base.h.
// This sketch publishes "none" as sensor type — use it to verify
// provisioning, MQTT, and OTA work before adding sensor code.

#define SENSOR_NAME "none"

#include "base.h"

void setup() {
  Serial.begin(115200);
  baseSetup();
}

void loop() {
  baseLoop();
  // No sensor to publish — connection health is enough for baseline testing.
}
