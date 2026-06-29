#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PWD, MQTT_SERVER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD
#include "led.h"

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

  // Follow GitHub redirect to CDN
  String finalUrl = url;
  WiFiClientSecure redirectClient;
  redirectClient.setInsecure();
  HTTPClient http;
  http.begin(redirectClient, finalUrl);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK || httpCode == 302 || httpCode == 301) {
    finalUrl = http.getLocation();
    if (finalUrl.isEmpty()) finalUrl = url;
  }
  http.end();

  Serial.print("Final OTA URL: ");
  Serial.println(finalUrl);

  otaClient.setInsecure();
  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret = httpUpdate.update(otaClient, finalUrl);

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
  char message[512];
  unsigned int len = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
  for (unsigned int i = 0; i < len; i++) message[i] = (char)payload[i];
  message[len] = '\0';

  if (strcmp(topic, "devices/ota") == 0) {
    char msgDeviceId[50];
    char msgUrl[512];
    int matched = sscanf(message, "%49s %511s", msgDeviceId, msgUrl);
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
      snprintf(sensorPayload, sizeof(sensorPayload), "%s none", DEVICE_ID);
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
  ledSetup();
  ConnectToWiFi();
  SetupNTP();
  wifiClient.setInsecure();
  mqttClient.setBufferSize(512);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(CallbackMqtt);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) ConnectToWiFi();
  if (!mqttClient.connected()) ConnectToMqtt();
  mqttClient.loop();
}
