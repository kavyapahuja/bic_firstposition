#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "sensor.h"
#include "features.h"
#include "model.h"
#include "../logic/sensor_fall_logic.h"

static float window[WINDOW_SIZE][FD_NUM_AXES];
static int samplesInWindow = 0;
static unsigned long lastSampleTime = 0;

static void checkWindowForFall() {
  float features[FD_NUM_FEATURES];

  fd_extract_features(&window[0][0], WINDOW_SIZE, features);

  const int predictedClass = fd_predict(features, nullptr);
  const float peakAccelerationG =
      fd_peak_acc_g(&window[0][0], WINDOW_SIZE);

  const bool fallDetected =
      sensorFallDetected(predictedClass, peakAccelerationG);

  Serial.print("Activity: ");
  Serial.print(FD_CLASS_NAMES[predictedClass]);
  Serial.print(" | Peak acceleration: ");
  Serial.print(peakAccelerationG);
  Serial.println(" g");

  if (fallDetected) {
    Serial.println("SENSOR FALL DETECTED");
  }
}

static void moveWindowForward() {
  memmove(
      window,
      window[WINDOW_STEP_SIZE],
      (WINDOW_SIZE - WINDOW_STEP_SIZE) * FD_NUM_AXES * sizeof(float)
  );

  // Keep the newest half of the previous window.
  samplesInWindow = WINDOW_SIZE - WINDOW_STEP_SIZE;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!sensorBegin()) {
    Serial.println("Sensor setup failed. Check the wiring and pin values.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("DWINETRA sensor detection started");
}

void loop() {
  const unsigned long now = millis();

  if (now - lastSampleTime < SAMPLE_PERIOD_MS) {
    return;
  }

  lastSampleTime = now;

  SensorSample sample;

  if (!sensorRead(sample)) {
    Serial.println("Could not read the sensor");
    return;
  }

  memcpy(window[samplesInWindow], sample.values, sizeof(sample.values));
  samplesInWindow++;

  if (samplesInWindow == WINDOW_SIZE) {
    checkWindowForFall();
    moveWindowForward();
  }
}