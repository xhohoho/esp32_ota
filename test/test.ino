#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PWD, MQTT_SERVER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD, DEVICE_ID
#include "led.h"

// ═══════════════════════════════════════════════════════════
// BUMP THIS before each test export/upload so you can visually
// confirm the OTA actually replaced the firmware (check Serial
// Monitor or devices/status after the update completes).
// ═══════════════════════════════════════════════════════════
const char *FIRMWARE_VERSION = "v1";

const char *SENSOR_NAME = "test";

const char *mqtt_username = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;

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
  httpUpdate.rebootOnUpdate(true);

  ledOtaDownloading();
  t_httpUpdate_return ret = httpUpdate.update(otaClient, url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      publishStatus("ota failed");
      ledOtaFail();
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
// DUMMY SENSOR PUBLISH
// ═══════════════════════════════════════════════════════════
void publishSensor(const char *timestamp) {
  // Random dummy value just to prove data is flowing end-to-end.
  float value = random(0, 10000) / 100.0;   // 0.00 - 99.99

  char payload[180];
  snprintf(payload, sizeof(payload),
    "{\"device\":\"%s\",\"sensor\":\"%s\",\"firmware\":\"%s\",\"timestamp\":\"%s\","
    "\"value\":%.2f}",
    DEVICE_ID, SENSOR_NAME, FIRMWARE_VERSION, timestamp, value);
  if (mqttClient.publish("devices/data", payload)) {
    ledDataSent();
  } else {
    ledPublishFail();
  }
  Serial.println(payload);
}

// ═══════════════════════════════════════════════════════════
// MQTT
// ═══════════════════════════════════════════════════════════
void publishStatus(const char *status) {
  char payload[180];
  snprintf(payload, sizeof(payload), "%s %s (fw %s)", DEVICE_ID, status, FIRMWARE_VERSION);
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
    char lwtPayload[50];
    snprintf(lwtPayload, sizeof(lwtPayload), "%s offline", DEVICE_ID);
    if (mqttClient.connect(DEVICE_ID, mqtt_username, mqtt_password, "devices/status", 1, true, lwtPayload)) {
      Serial.println("MQTT connected.");
      mqttClient.subscribe("devices/ota");

      char statusPayload[50];
      snprintf(statusPayload, sizeof(statusPayload), "%s online", DEVICE_ID);
      mqttClient.publish("devices/status", statusPayload, true);

      char sensorPayload[50];
      snprintf(sensorPayload, sizeof(sensorPayload), "%s test", DEVICE_ID);
      mqttClient.publish("devices/sensor", sensorPayload, true);

      ledMqttOK();
    } else {
      Serial.print("Failed rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2s...");
      ledMqttConnecting();
    }
  }
}

void ConnectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PWD, 6);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    ledWifiConnecting();
  }
  Serial.println("\nWiFi connected.");
  ledWifiOK();
}

void SetupNTP() {
  configTime(8 * 3600, 0, "pool.ntp.org");
  Serial.print("Syncing NTP...");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    ledNtpSyncing();
  }
  Serial.println("\nNTP synced.");
}

// ═══════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.print("Firmware version: ");
  Serial.println(FIRMWARE_VERSION);

  ledSetup();
  ConnectToWiFi();
  SetupNTP();
  randomSeed(esp_random());

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

  if (now - last_time > 2000) {
    last_time = now;
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &timeinfo);
    publishSensor(timestamp);
  }
}
