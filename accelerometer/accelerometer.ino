// accelerometer.ino — MPU6050 accelerometer + gyroscope sensor
// All WiFi/MQTT/OTA/NVS logic lives in base.h.
// Only publishSensor() and sensor init are defined here.

#define SENSOR_NAME "accelerometer"

#include "base.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Default ESP32 I2C pins
const byte SDA_PIN = 21;
const byte SCL_PIN = 22;

Adafruit_MPU6050 mpu;

void publishSensor(const char *timestamp) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  char payload[280];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"timestamp\":\"%s\","
    "\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f,"
    "\"gyro_x\":%.3f,\"gyro_y\":%.3f,\"gyro_z\":%.3f,"
    "\"temp_c\":%.2f}",
    deviceIdBuf, SENSOR_NAME, timestamp,
    accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
    gyro.gyro.x, gyro.gyro.y, gyro.gyro.z,
    temp.temperature);
  if (mqttClient.publish("devices/data", payload)) {
    ledDataSent();
  } else {
    ledPublishFail();
  }
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found — check wiring!");
    while (true) { ledSensorError(); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 ready.");

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
