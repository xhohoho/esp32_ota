// base.h — shared WiFi/MQTT/OTA/NVS foundation for all ESP32 OTA sketches
//
// USAGE: #include "base.h" at the top of each sensor sketch.
// Each sketch only needs to implement publishSensor(const char *timestamp).
//
// FIXES vs old per-sketch approach:
//   - WiFiManagerParameter objects allocated once, never leaked
//   - ConnectToWiFi() is idempotent (safe to call repeatedly)
//   - MQTT reconnect uses exponential backoff (2s→4s→8s…cap 60s)
//   - publishStatus() guards against publishing while disconnected
//   - No secrets.h required — all creds stored in NVS flash

#pragma once

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>
#include <WiFiManager.h>   // tzapu/WiFiManager — install via Library Manager
#include <Preferences.h>   // built-in, wraps ESP32 NVS flash storage
#include "led.h"

// ── clients ─────────────────────────────────────────────────────────────────

WiFiClientSecure wifiClient;
WiFiClientSecure otaClient;
PubSubClient     mqttClient(wifiClient);
WiFiManager      wm;
Preferences      prefs;

// ── config ───────────────────────────────────────────────────────────────────

// Hold GPIO0 (BOOT button on most devkits) LOW on power-up to wipe
// saved WiFi + MQTT settings and re-enter the setup portal.
// Device ID is kept across this reset.
#define RESET_BUTTON_PIN 0

// Auto-recovery: reopen the portal if credentials keep failing.
#define WIFI_FAIL_THRESHOLD  40   // ~20 s at 500 ms/retry
#define MQTT_FAIL_THRESHOLD  10   // attempts before escalating to portal

// ── runtime buffers (filled from NVS or WiFiManager portal) ──────────────────

char deviceIdBuf[32]   = "";
char mqttServerBuf[64] = "";
char mqttPortBuf[6]    = "8883";
char mqttUserBuf[64]   = "";
char mqttPassBuf[64]   = "";
int  mqttPort          = 8883;

// ── WiFiManager params (allocated once) ──────────────────────────────────────

static WiFiManagerParameter p_devid ("devid",  "Device ID",    deviceIdBuf,   sizeof(deviceIdBuf));
static WiFiManagerParameter p_server("server", "MQTT server",  mqttServerBuf, sizeof(mqttServerBuf));
static WiFiManagerParameter p_port  ("port",   "MQTT port",    mqttPortBuf,   sizeof(mqttPortBuf));
static WiFiManagerParameter p_user  ("user",   "MQTT username",mqttUserBuf,   sizeof(mqttUserBuf));
static WiFiManagerParameter p_pass  ("pass",   "MQTT password",mqttPassBuf,   sizeof(mqttPassBuf));

static bool paramsAdded     = false;
static bool shouldSaveConfig = false;

void saveConfigCallback() { shouldSaveConfig = true; }

// ── NVS helpers ──────────────────────────────────────────────────────────────

void generateDefaultDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  uint8_t b1 = (mac >> 8)  & 0xFF;
  uint8_t b2 = (mac >> 16) & 0xFF;
  uint8_t b3 = (mac >> 24) & 0xFF;
  snprintf(deviceIdBuf, sizeof(deviceIdBuf), "esp32-%02X%02X%02X", b1, b2, b3);
}

void loadPrefs() {
  // MQTT creds — wiped on provisioning reset
  prefs.begin("mqtt", true);
  prefs.getString("server", mqttServerBuf, sizeof(mqttServerBuf));
  prefs.getString("port",   mqttPortBuf,   sizeof(mqttPortBuf));
  prefs.getString("user",   mqttUserBuf,   sizeof(mqttUserBuf));
  prefs.getString("pass",   mqttPassBuf,   sizeof(mqttPassBuf));
  prefs.end();

  if (strlen(mqttPortBuf) == 0) strcpy(mqttPortBuf, "8883");
  mqttPort = atoi(mqttPortBuf);

  // Device ID — survives a provisioning reset
  prefs.begin("device", true);
  prefs.getString("devid", deviceIdBuf, sizeof(deviceIdBuf));
  prefs.end();

  if (strlen(deviceIdBuf) == 0) generateDefaultDeviceId();
}

void saveMqttPrefs() {
  prefs.begin("mqtt", false);
  prefs.putString("server", mqttServerBuf);
  prefs.putString("port",   mqttPortBuf);
  prefs.putString("user",   mqttUserBuf);
  prefs.putString("pass",   mqttPassBuf);
  prefs.end();
  mqttPort = atoi(mqttPortBuf);
}

void saveDeviceIdPref() {
  prefs.begin("device", false);
  prefs.putString("devid", deviceIdBuf);
  prefs.end();
}

// ── OTA ──────────────────────────────────────────────────────────────────────

// Forward declaration — defined below
void publishStatus(const char *status);

void performOTA(const char *url) {
  publishStatus("ota downloading");
  Serial.print("OTA URL: "); Serial.println(url);

  // Resolve any GitHub redirect to the final CDN URL
  String finalUrl = url;
  WiFiClientSecure redirectClient;
  redirectClient.setInsecure();
  HTTPClient http;
  http.begin(redirectClient, finalUrl);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code == HTTP_CODE_OK || code == 302 || code == 301) {
    String loc = http.getLocation();
    if (!loc.isEmpty()) finalUrl = loc;
  }
  http.end();

  Serial.print("Final OTA URL: "); Serial.println(finalUrl);

  otaClient.setInsecure();  // no CA bundle on device; fine for firmware CDN
  httpUpdate.rebootOnUpdate(true);
  ledOtaDownloading();

  t_httpUpdate_return ret = httpUpdate.update(otaClient, finalUrl);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Failed (%d): %s\n",
        httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      publishStatus("ota failed");
      ledOtaFail();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      publishStatus("ota no update");
      break;
    case HTTP_UPDATE_OK:
      publishStatus("ota success");  // device reboots immediately after
      break;
  }
}

// ── MQTT ─────────────────────────────────────────────────────────────────────

void publishStatus(const char *status) {
  if (!mqttClient.connected()) return;  // guard: don't publish while disconnected
  char payload[150];
  snprintf(payload, sizeof(payload), "%s %s", deviceIdBuf, status);
  mqttClient.publish("devices/status", payload, true);
  Serial.print("Status: "); Serial.println(payload);
}

void CallbackMqtt(char *topic, byte *payload, unsigned int length) {
  char message[512];
  unsigned int len = min(length, (unsigned int)sizeof(message) - 1);
  memcpy(message, payload, len);
  message[len] = '\0';

  if (strcmp(topic, "devices/ota") == 0) {
    char msgDeviceId[50], msgUrl[512];
    if (sscanf(message, "%49s %511s", msgDeviceId, msgUrl) == 2
        && strcmp(msgDeviceId, deviceIdBuf) == 0) {
      performOTA(msgUrl);
    }
  }
}

// Forward declaration — ConnectToMqtt() can escalate to portal
void ConnectToWiFi(bool forcePortal);

void ConnectToMqtt() {
  Serial.println("Connecting to MQTT...");
  int failCount   = 0;
  int backoffMs   = 2000;  // starts at 2s, doubles each attempt, caps at 60s

  while (!mqttClient.connected()) {
    char lwtPayload[50];
    snprintf(lwtPayload, sizeof(lwtPayload), "%s offline", deviceIdBuf);

    if (mqttClient.connect(deviceIdBuf, mqttUserBuf, mqttPassBuf,
                           "devices/status", 1, true, lwtPayload)) {
      Serial.println("MQTT connected.");
      mqttClient.subscribe("devices/ota");

      // Announce online + sensor type
      char buf[50];
      snprintf(buf, sizeof(buf), "%s online", deviceIdBuf);
      mqttClient.publish("devices/status", buf, true);

      // SENSOR_NAME must be defined in the including sketch
      snprintf(buf, sizeof(buf), "%s %s", deviceIdBuf, SENSOR_NAME);
      mqttClient.publish("devices/sensor", buf, true);

      ledMqttOK();
      failCount  = 0;
      backoffMs  = 2000;
    } else {
      Serial.printf("MQTT failed rc=%d, retry in %dms...\n",
        mqttClient.state(), backoffMs);
      ledMqttConnecting();
      delay(backoffMs);
      backoffMs = min(backoffMs * 2, 60000);  // exponential backoff, cap 60s

      failCount++;
      if (failCount >= MQTT_FAIL_THRESHOLD) {
        Serial.println("MQTT repeatedly failing — reopening setup portal...");
        ConnectToWiFi(true);
        mqttClient.setServer(mqttServerBuf, mqttPort);
        failCount = 0;
        backoffMs = 2000;
      }
    }
  }
}

// ── provisioning ─────────────────────────────────────────────────────────────

void checkForProvisioningReset() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Reset button held — hold 3s to confirm erase...");
    delay(3000);
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      Serial.println("Erasing WiFi + MQTT settings (device ID kept)...");
      wm.resetSettings();
      prefs.begin("mqtt", false); prefs.clear(); prefs.end();
      ledOtaFail();
      delay(500);
      ESP.restart();
    }
  }
}

void ConnectToWiFi(bool forcePortal) {
  Serial.println("Starting WiFiManager...");
  loadPrefs();

  // Sync current NVS values into the parameter defaults
  p_devid .setValue(deviceIdBuf,   sizeof(deviceIdBuf));
  p_server.setValue(mqttServerBuf, sizeof(mqttServerBuf));
  p_port  .setValue(mqttPortBuf,   sizeof(mqttPortBuf));
  p_user  .setValue(mqttUserBuf,   sizeof(mqttUserBuf));
  p_pass  .setValue(mqttPassBuf,   sizeof(mqttPassBuf));

  // Add params only once — WiFiManager accumulates duplicates otherwise
  if (!paramsAdded) {
    wm.addParameter(&p_devid);
    wm.addParameter(&p_server);
    wm.addParameter(&p_port);
    wm.addParameter(&p_user);
    wm.addParameter(&p_pass);
    paramsAdded = true;
  }

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);

  String apName = String(deviceIdBuf);
  bool noMqttYet = (strlen(mqttServerBuf) == 0);
  bool openPortal = forcePortal || noMqttYet;

  bool connected = openPortal
    ? wm.startConfigPortal(apName.c_str())
    : wm.autoConnect(apName.c_str());

  if (!connected) {
    Serial.println("Portal timed out / connect failed. Restarting...");
    delay(1000);
    ESP.restart();
  }

  if (shouldSaveConfig) {
    strncpy(deviceIdBuf,   p_devid .getValue(), sizeof(deviceIdBuf)   - 1);
    strncpy(mqttServerBuf, p_server.getValue(), sizeof(mqttServerBuf) - 1);
    strncpy(mqttPortBuf,   p_port  .getValue(), sizeof(mqttPortBuf)   - 1);
    strncpy(mqttUserBuf,   p_user  .getValue(), sizeof(mqttUserBuf)   - 1);
    strncpy(mqttPassBuf,   p_pass  .getValue(), sizeof(mqttPassBuf)   - 1);
    if (strlen(deviceIdBuf) == 0) generateDefaultDeviceId();
    saveMqttPrefs();
    saveDeviceIdPref();
    shouldSaveConfig = false;
    Serial.println("Config saved to flash.");
  }

  Serial.print("WiFi connected. IP: "); Serial.println(WiFi.localIP());
  Serial.print("Device ID: ");          Serial.println(deviceIdBuf);
  ledWifiOK();
}

// ── NTP ──────────────────────────────────────────────────────────────────────

void SetupNTP() {
  configTime(8 * 3600, 0, "pool.ntp.org");
  Serial.print("Syncing NTP...");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) { Serial.print("."); ledNtpSyncing(); }
  Serial.println("\nNTP synced.");
}

// ── shared setup / loop helpers ───────────────────────────────────────────────
// Call baseSetup() at the end of setup() in your sketch (after sensor init).
// Call baseLoop() at the top of loop() before your sensor logic.

void baseSetup() {
  ledSetup();
  checkForProvisioningReset();
  ConnectToWiFi(false);
  SetupNTP();
  wifiClient.setInsecure();  // no CA bundle — acceptable for this use case
  mqttClient.setBufferSize(512);
  mqttClient.setServer(mqttServerBuf, mqttPort);
  mqttClient.setCallback(CallbackMqtt);
  ConnectToMqtt();
}

void baseLoop() {
  static int wifiFailCount = 0;

  if (WiFi.status() != WL_CONNECTED) {
    ledWifiConnecting();
    wifiFailCount++;
    if (wifiFailCount >= WIFI_FAIL_THRESHOLD) {
      Serial.println("WiFi repeatedly failing — reopening setup portal...");
      ConnectToWiFi(true);
      mqttClient.setServer(mqttServerBuf, mqttPort);
      wifiFailCount = 0;
    } else {
      delay(500);
    }
    return;  // skip MQTT until WiFi is back
  }
  wifiFailCount = 0;

  if (!mqttClient.connected()) ConnectToMqtt();
  mqttClient.loop();
}
