#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PWD, MQTT_SERVER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD, DEVICE_ID

const char *SENSOR_NAME = "accelerometer";

const char *mqtt_username = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;

// ═══════════════════════════════════════════════════════════
// SENSOR (MPU6050 over I2C)
// ═══════════════════════════════════════════════════════════
// Default ESP32 I2C pins: SDA = 21, SCL = 22
const byte SDA_PIN = 21;
const byte SCL_PIN = 22;

Adafruit_MPU6050 mpu;

WiFiClientSecure wifiClient;
WiFiClientSecure otaClient;
PubSubClient mqttClient(wifiClient);

// ═══════════════════════════════════════════════════════════
// OTA
// ═══════════════════════════════════════════════════════════
void publishStatus(const char *status);

void performOTA(const char *url) {
  publishStatus("ota downloading");
  Serial.print("OTA URL: ");
  Serial.println(url);

  otaClient.setInsecure();
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret = httpUpdate.update(otaClient, url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      publishStatus("ota failed");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      publishStatus("ota no update");
      break;
    case HTTP_UPDATE_OK:
      publishStatus("ota success");
      break;
  }
}

// ═══════════════════════════════════════════════════════════
// SENSOR PUBLISH
// ═══════════════════════════════════════════════════════════
void publishSensor(const char *timestamp) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  char payload[280];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"timestamp\":\"%s\","
    "\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f,"
    "\"gyro_x\":%.3f,\"gyro_y\":%.3f,\"gyro_z\":%.3f,"
    "\"temp_c\":%.2f}",
    DEVICE_ID, SENSOR_NAME, timestamp,
    accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
    gyro.gyro.x, gyro.gyro.y, gyro.gyro.z,
    temp.temperature);
  mqttClient.publish("devices/data", payload);
  Serial.println(payload);
}

// ═══════════════════════════════════════════════════════════
// MQTT
// ═══════════════════════════════════════════════════════════
void publishStatus(const char *status) {
  char payload[150];
  snprintf(payload, sizeof(payload), "%s %s", DEVICE_ID, status);
  mqttClient.publish("devices/status", payload, true);
  Serial.print("Status: ");
  Serial.println(payload);
}

void CallbackMqtt(char *topic, byte *payload, unsigned int length) {
  char message[200];
  unsigned int len = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
  for (unsigned int i = 0; i < len; i++) message[i] = (char)payload[i];
  message[len] = '\0';

  if (strcmp(topic, "devices/ota") == 0) {
    char msgDeviceId[50];
    char msgUrl[150];
    int matched = sscanf(message, "%s %s", msgDeviceId, msgUrl);
    if (matched == 2 && strcmp(msgDeviceId, DEVICE_ID) == 0) {
      performOTA(msgUrl);
    }
  }
}

void ConnectToMqtt() {
  Serial.println("Connecting to MQTT...");
  while (!mqttClient.connected()) {
    char clientId[50];
    sprintf(clientId, "ESP32Client-%04X", random(0xffff));
    if (mqttClient.connect(clientId, mqtt_username, mqtt_password)) {
      Serial.println("MQTT connected.");
      mqttClient.subscribe("devices/ota");

      char payload[50];
      snprintf(payload, sizeof(payload), "%s online", DEVICE_ID);
      mqttClient.publish("devices/connected", payload, true);

      char status[50];
      snprintf(status, sizeof(status), "running %s", SENSOR_NAME);
      publishStatus(status);
    } else {
      Serial.print("Failed rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2s...");
      delay(2000);
    }
  }
}

void ConnectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PWD, 6);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected.");
}

void SetupNTP() {
  configTime(8 * 3600, 0, "pool.ntp.org");
  Serial.print("Syncing NTP...");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nNTP synced.");
}

// ═══════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  ConnectToWiFi();
  SetupNTP();

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found — check wiring!");
    while (true) delay(1000);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 ready.");

  wifiClient.setInsecure();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(CallbackMqtt);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) ConnectToWiFi();
  if (!mqttClient.connected()) ConnectToMqtt();
  mqttClient.loop();

  static uint64_t last_time = 0;
  uint64_t now = millis();

  if (now - last_time > 1000) {
    last_time = now;
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &timeinfo);
    publishSensor(timestamp);
  }
}
