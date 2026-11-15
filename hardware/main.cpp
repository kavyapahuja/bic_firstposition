#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "sensor.h"
#include "features.h"
#include "model.h"
#include "wifi_status.h"
#include "../logic/sensor_fall_logic.h"

static float window[WINDOW_SIZE][FD_NUM_AXES];
static int samplesInWindow = 0;
static unsigned long lastSampleTime = 0;
static unsigned long lastFallMessageTime = 0;
static int samplesSinceReadingMessage = 0;

static float classDecisionScore(const float *rawFeatures, int classIndex) {
  float score = FD_INTERCEPT[classIndex];

  for (int i = 0; i < FD_NUM_FEATURES; i++) {
    const float scaled =
        (rawFeatures[i] - FD_SCALER_MEAN[i]) / FD_SCALER_SCALE[i];
    score += scaled * FD_COEF[classIndex][i];
  }

  return score;
}

static void checkWindowForFall() {
  float features[FD_NUM_FEATURES];

  fd_extract_features(&window[0][0], WINDOW_SIZE, features);

  float winningScore = 0.0f;
  const int predictedClass = fd_predict(features, &winningScore);

  const float fallScore = classDecisionScore(features, FD_FALL_CLASS);

  float highestOtherScore = 0.0f;
  bool foundOtherClass = false;

  for (int classIndex = 0; classIndex < FD_NUM_CLASSES; classIndex++) {
    if (classIndex == FD_FALL_CLASS) {
      continue;
    }

    const float score = classDecisionScore(features, classIndex);
    if (!foundOtherClass || score > highestOtherScore) {
      highestOtherScore = score;
      foundOtherClass = true;
    }
  }

  const float fallMargin = fallScore - highestOtherScore;

  const float peakAccelerationG =
      fd_peak_acc_g(&window[0][0], WINDOW_SIZE);

  const char *activity = FD_CLASS_NAMES[predictedClass];

  Serial.print("Activity: ");
  Serial.print(activity);
  Serial.print(" | Peak acceleration: ");
  Serial.print(peakAccelerationG, 2);
  Serial.println(" g");

  Serial.print("Winning score: ");
  Serial.print(winningScore, 3);
  Serial.print(" | Fall score: ");
  Serial.print(fallScore, 3);
  Serial.print(" | Fall margin: ");
  Serial.println(fallMargin, 3);

  wifiSendActivity(activity, peakAccelerationG);

  const bool fallDetected =
      sensorFallDetected(predictedClass, peakAccelerationG);

  if (fallDetected &&
      (lastFallMessageTime == 0 ||
       millis() - lastFallMessageTime >= ALERT_COOLDOWN_MS)) {
    Serial.println("SENSOR FALL DETECTED");
    wifiSendFall(peakAccelerationG);
    lastFallMessageTime = millis();
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
  Serial.println("DWINETRA sensor starting");

  if (!sensorBegin()) {
    Serial.println("Sensor setup failed. Check the I2C pins and wiring.");
    while (true) {
      delay(1000);
    }
  }

  wifiStatusBegin();
  Serial.println("Sensor ready. Wi-Fi terminal messages will start after connection.");
}

void loop() {
  wifiStatusService();

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

  // Send one raw sample for every ten sensor samples: about 10 messages/second.
  samplesSinceReadingMessage++;
  if (samplesSinceReadingMessage >= SAMPLE_RATE_HZ) {
    samplesSinceReadingMessage = 0;
    wifiSendReading(sample);
  }

  memcpy(window[samplesInWindow], sample.values, sizeof(sample.values));
  samplesInWindow++;

  if (samplesInWindow == WINDOW_SIZE) {
    checkWindowForFall();
    moveWindowForward();
  }
}