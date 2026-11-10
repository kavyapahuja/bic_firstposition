#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "secrets.h"
#include "wifi_status.h"

namespace {
WiFiUDP udp;
IPAddress laptopAddress;

bool laptopAddressIsValid = false;
bool wasConnected = false;
unsigned long lastConnectionAttempt = 0;

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;

bool canSend() {
  return laptopAddressIsValid && WiFi.status() == WL_CONNECTED;
}

void sendPacket(const String &message) {
  if (!canSend()) {
    return;
  }

  if (!udp.beginPacket(laptopAddress, WIFI_STATUS_PORT)) {
    return;
  }

  udp.print(message);
  udp.endPacket();
}
}

void wifiStatusBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  laptopAddressIsValid = laptopAddress.fromString(LAPTOP_IP);
  if (!laptopAddressIsValid) {
    Serial.println("[wifi] laptop IP in secrets.h is not valid");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastConnectionAttempt = millis();

  Serial.println("[wifi] connecting");
}

void wifiStatusService() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      Serial.print("[wifi] connected; ESP32 IP: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
    return;
  }

  if (wasConnected) {
    Serial.println("[wifi] connection lost; sensor reading continues");
    wasConnected = false;
  }

  if (millis() - lastConnectionAttempt >= WIFI_RETRY_INTERVAL_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastConnectionAttempt = millis();
    Serial.println("[wifi] retrying connection");
  }
}

void wifiSendReading(const SensorSample &sample) {
  String message = "{\"type\":\"reading\",\"ax_g\":";
  message += String(sample.values[0], 3);
  message += ",\"ay_g\":";
  message += String(sample.values[1], 3);
  message += ",\"az_g\":";
  message += String(sample.values[2], 3);
  message += ",\"gx_dps\":";
  message += String(sample.values[3], 2);
  message += ",\"gy_dps\":";
  message += String(sample.values[4], 2);
  message += ",\"gz_dps\":";
  message += String(sample.values[5], 2);
  message += "}";

  sendPacket(message);
}

void wifiSendActivity(const char *activity, float peakAccelerationG) {
  String message = "{\"type\":\"activity\",\"class\":\"";
  message += activity;
  message += "\",\"peak_g\":";
  message += String(peakAccelerationG, 2);
  message += "}";

  sendPacket(message);
}

void wifiSendFall(float peakAccelerationG) {
  String message = "{\"type\":\"fall\",\"peak_g\":";
  message += String(peakAccelerationG, 2);
  message += "}";

  sendPacket(message);
}