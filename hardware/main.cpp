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
static int samplesSinceRawPrint = 0;

static void printRawReading(const SensorSample &sample) {
  Serial.print("RAW | ax=");
  Serial.print(sample.values[0], 3);
  Serial.print(" ay=");
  Serial.print(sample.values[1], 3);
  Serial.print(" az=");
  Serial.print(sample.values[2], 3);
  Serial.print(" g | gx=");
  Serial.print(sample.values[3], 2);
  Serial.print(" gy=");
  Serial.print(sample.values[4], 2);
  Serial.print(" gz=");
  Serial.print(sample.values[5], 2);
  Serial.println(" degrees/second");
}

static void checkWindowForFall() {
  float features[FD_NUM_FEATURES];

  fd_extract_features(&window[0][0], WINDOW_SIZE, features);

  const int predictedClass = fd_predict(features, nullptr);
  const float peakAccelerationG =
      fd_peak_acc_g(&window[0][0], WINDOW_SIZE);

  Serial.print("Activity: ");
  Serial.print(FD_CLASS_NAMES[predictedClass]);
  Serial.print(" | Peak acceleration: ");
  Serial.print(peakAccelerationG, 2);
  Serial.println(" g");

  if (sensorFallDetected(predictedClass, peakAccelerationG)) {
    Serial.println("SENSOR FALL DETECTED");
  }
}

static void moveWindowForward() {
  memmove(
      window,
      window[WINDOW_STEP_SIZE],
      (WINDOW_SIZE - WINDOW_STEP_SIZE) * FD_NUM_AXES * sizeof(float)
  );

  samplesInWindow = WINDOW_SIZE - WINDOW_STEP_SIZE;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("DWINETRA sensor check starting");

  if (!sensorBegin()) {
    Serial.println("Sensor setup failed. Check the I2C pins and wiring.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Sensor ready. Leave it still for the first readings.");
}

void loop() {
  const unsigned long now = millis();

  if (now - lastSampleTime < SAMPLE_PERIOD_MS) {
    return;
  }

  lastSampleTime = now;

  SensorSample sample;

  if (!sensorRead(sample)) {
    Serial.println("Could not read the MPU6050");
    return;
  }

  // Print every 10th sample: about 10 lines per second at a 100 Hz sample rate.
  samplesSinceRawPrint++;
  if (samplesSinceRawPrint >= SAMPLE_RATE_HZ / 10) {
    samplesSinceRawPrint = 0;
    printRawReading(sample);
  }

  memcpy(window[samplesInWindow], sample.values, sizeof(sample.values));
  samplesInWindow++;

  if (samplesInWindow == WINDOW_SIZE) {
    checkWindowForFall();
    moveWindowForward();
  }
}