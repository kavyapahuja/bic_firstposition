#pragma once

#include "sensor.h"

void wifiStatusBegin();
void wifiStatusService();

void wifiSendReading(const SensorSample &sample);
void wifiSendActivity(const char *activity, float peakAccelerationG);
void wifiSendFall(float peakAccelerationG);