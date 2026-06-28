// led.h — LED indicator patterns for ESP32 OTA sketches
// Uses LED_BUILTIN (GPIO 2 on most ESP32 dev boards).
// Include this file once in your .ino, then call ledSetup() in setup().

#pragma once
#include <Arduino.h>

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// ── primitives ──────────────────────────────────────────────────────────────

inline void ledSetup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

inline void ledOn()  { digitalWrite(LED_BUILTIN, HIGH); }
inline void ledOff() { digitalWrite(LED_BUILTIN, LOW);  }

// Blink n times at a given on/off period (ms), then leave LED off.
inline void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    ledOn();  delay(onMs);
    ledOff(); delay(offMs);
  }
}

// ── named states ─────────────────────────────────────────────────────────────

// Connecting to WiFi — fast single blink loop (call repeatedly inside while)
inline void ledWifiConnecting()  { ledBlink(1, 100, 100); }

// WiFi just connected — 3 quick flashes
inline void ledWifiOK()          { ledBlink(3, 80, 80); }

// NTP syncing — slow single blink loop (call repeatedly inside while)
inline void ledNtpSyncing()      { ledBlink(1, 500, 500); }

// Connecting to MQTT — double blink loop (call repeatedly inside while)
inline void ledMqttConnecting()  { ledBlink(2, 120, 120); delay(400); }

// MQTT just connected — 5 quick flashes
inline void ledMqttOK()          { ledBlink(5, 80, 80); }

// Data sent successfully — single short flash
inline void ledDataSent()        { ledBlink(1, 60, 0); }

// MQTT publish failed — 3 medium flashes
inline void ledPublishFail()     { ledBlink(3, 300, 150); }

// OTA in progress — rapid blink (call repeatedly or just show once)
inline void ledOtaDownloading()  { ledBlink(1, 50, 50); }

// OTA failed — 3 long flashes
inline void ledOtaFail()         { ledBlink(3, 600, 200); }

// Sensor error / not found — 5 long flashes
inline void ledSensorError()     { ledBlink(5, 500, 200); }
