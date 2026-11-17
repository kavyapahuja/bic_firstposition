#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdio.h>

#include "config.h"
#include "secrets.h"
#include "wifi_status.h"

namespace {

WiFiUDP udp;
IPAddress laptopAddress;

bool laptopAddressIsValid = false;
bool wasConnected = false;
unsigned long lastConnectionAttempt = 0;
unsigned long lastSendWarning = 0;

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;

// Fall message waiting to be repeated.
char pendingFallMessage[160];
int pendingFallRepeats = 0;
unsigned long nextFallRepeatTime = 0;

bool canSend() {
  return laptopAddressIsValid && WiFi.status() == WL_CONNECTED;
}

void sendPacket(const char *message) {
  if (!canSend()) {
    // Print at most once every 5 seconds so the Serial Monitor stays readable.
    if (millis() - lastSendWarning >= 5000UL) {
      lastSendWarning = millis();
      Serial.println("[wifi] Cannot send: Wi-Fi disconnected or invalid IP");
    }
    return;
  }

  if (!udp.beginPacket(laptopAddress, WIFI_STATUS_PORT)) {
    Serial.println("[wifi] beginPacket FAILED");
    return;
  }

  udp.print(message);

  if (udp.endPacket() == 1) {
    static bool firstSuccess = true;

    if (firstSuccess) {
      Serial.println("[wifi] UDP packet sent successfully");
      firstSuccess = false;
    }
  } else {
    Serial.println("[wifi] UDP packet FAILED");
  }
}

void sendPendingFallRepeats() {
  if (pendingFallRepeats <= 0) {
    return;
  }

  if ((long)(millis() - nextFallRepeatTime) < 0) {
    return;
  }

  sendPacket(pendingFallMessage);
  pendingFallRepeats--;
  nextFallRepeatTime = millis() + FALL_REPEAT_INTERVAL_MS;
}

}  // namespace


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
  sendPendingFallRepeats();

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
  char message[200];

  snprintf(message, sizeof(message),
           "{\"type\":\"reading\",\"ax_g\":%.3f,\"ay_g\":%.3f,\"az_g\":%.3f,"
           "\"gx_dps\":%.2f,\"gy_dps\":%.2f,\"gz_dps\":%.2f}",
           sample.values[0], sample.values[1], sample.values[2],
           sample.values[3], sample.values[4], sample.values[5]);

  sendPacket(message);
}

void wifiSendActivity(const char *activity, float peakAccelerationG) {
  char message[120];

  snprintf(message, sizeof(message),
           "{\"type\":\"activity\",\"class\":\"%s\",\"peak_g\":%.2f}",
           activity, peakAccelerationG);

  sendPacket(message);
}

void wifiSendFall(uint32_t eventId, float peakAccelerationG) {
  snprintf(pendingFallMessage, sizeof(pendingFallMessage),
           "{\"type\":\"fall\",\"id\":%lu,\"peak_g\":%.2f}",
           (unsigned long)eventId, peakAccelerationG);

  sendPacket(pendingFallMessage);

  pendingFallRepeats = FALL_MESSAGE_REPEATS;
  nextFallRepeatTime = millis() + FALL_REPEAT_INTERVAL_MS;
}