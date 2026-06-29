// ultrasonic.ino — HC-SR04 ultrasonic distance sensor
// All WiFi/MQTT/OTA/NVS logic lives in base.h.
// Only publishSensor() and sensor init are defined here.

#define SENSOR_NAME "ultrasonic"

#include "../base.h"

const byte TRIG_PIN = 5;
const byte ECHO_PIN = 18;

void publishSensor(const char *timestamp) {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034f / 2.0f;

  char payload[200];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"timestamp\":\"%s\","
    "\"distance_cm\":%.2f}",
    deviceIdBuf, SENSOR_NAME, timestamp, distance);
  if (mqttClient.publish("devices/data", payload)) {
    ledDataSent();
  } else {
    ledPublishFail();
  }
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
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
