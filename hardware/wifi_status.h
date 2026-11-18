#pragma once

#include <Arduino.h>
#include "sensor.h"

void wifiStatusBegin();
void wifiStatusService();

void wifiSendReading(const SensorSample &sample);
void wifiSendActivity(const char *activity, float peakAccelerationG);
void wifiSendFall(uint32_t eventId, float peakAccelerationG);