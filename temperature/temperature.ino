// temperature.ino — DHT22 temperature + humidity sensor
// All WiFi/MQTT/OTA/NVS logic lives in base.h.
// Only publishSensor() and sensor init are defined here.

#define SENSOR_NAME "temperature"

#include "base.h"
#include <DHTesp.h>

const byte DHT_PIN = 15;
DHTesp dhtSensor;

void publishSensor(const char *timestamp) {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  char payload[200];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"timestamp\":\"%s\","
    "\"temperature\":%.2f,\"humidity\":%.2f}",
    deviceIdBuf, SENSOR_NAME, timestamp,
    data.temperature, data.humidity);
  if (mqttClient.publish("devices/data", payload)) {
    ledDataSent();
  } else {
    ledPublishFail();
  }
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  baseSetup();
}

void loop() {
  baseLoop();

  static uint64_t lastMs = 0;
  uint64_t now = millis();
  if (now - lastMs < 1000) return;
  lastMs = now;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &timeinfo);
  publishSensor(timestamp);
}
