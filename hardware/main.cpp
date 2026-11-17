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
static unsigned long nextSampleTime = 0;
static unsigned long lastFallMessageTime = 0;
static unsigned long lastDebugPrintTime = 0;
static int samplesSinceReadingMessage = 0;
static int consecutiveReadFailures = 0;

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
  const float peakAccelerationG = fd_peak_acc_g(&window[0][0], WINDOW_SIZE);
  const char *activity = FD_CLASS_NAMES[predictedClass];
  const bool modelSaysFall = (predictedClass == FD_FALL_CLASS);

  Serial.printf(
      "Activity: %s | Peak acceleration: %.2f g | Winning score: %.3f | "
      "Fall score: %.3f | Fall margin: %.3f\n",
      activity, peakAccelerationG, winningScore, fallScore, fallMargin);

  // Also acts as a heartbeat: the laptop can tell if the band stops sending.
  wifiSendActivity(activity, peakAccelerationG);

  // Impact + stillness check (see logic/sensor_fall_logic.cpp).
  const SensorFallResult result =
      sensorFallCheck(modelSaysFall, &window[0][0], WINDOW_SIZE);

  Serial.printf(
      "Fall check: %s | model: %s | impact %.2f g at sample %d | "
      "lowest before impact %.2f g | still acc std %.3f g | "
      "still gyro %.1f dps\n",
      result.reason, modelSaysFall ? "fall" : "no fall", result.impactG,
      result.impactIndex, result.preMinG, result.stillAccStdG,
      result.stillGyroMeanDps);

  const unsigned long now = millis();
  const bool cooldownOver =
      (lastFallMessageTime == 0) ||
      (now - lastFallMessageTime >= ALERT_COOLDOWN_MS);

  if (result.fall && cooldownOver) {
    Serial.println("SENSOR FALL DETECTED");
    wifiSendFall((uint32_t)now, result.impactG);
    lastFallMessageTime = now;
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
    // On a battery nobody is watching the Serial Monitor, so restart and try
    // again instead of freezing forever.
    Serial.println("Sensor setup failed. Check the I2C pins and wiring.");
    Serial.println("Restarting in 3 seconds...");
    delay(3000);
    ESP.restart();
  }

  wifiStatusBegin();
  Serial.println("Sensor ready. Wi-Fi terminal messages will start after connection.");

  nextSampleTime = millis();
}

void loop() {
  wifiStatusService();

  const unsigned long now = millis();
  if ((long)(now - nextSampleTime) < 0) {
    return;
  }

  // Keep a steady 100 Hz. If a slot was missed (for example during a slow
  // Wi-Fi send), skip ahead instead of reading in a burst.
  nextSampleTime += SAMPLE_PERIOD_MS;
  if ((long)(now - nextSampleTime) >= 0) {
    nextSampleTime = now + SAMPLE_PERIOD_MS;
  }

  SensorSample sample;
  if (!sensorRead(sample)) {
    consecutiveReadFailures++;

    if (consecutiveReadFailures == 1 || consecutiveReadFailures % 50 == 0) {
      Serial.println("Could not read the MPU6050");
    }

    // A gap in the data would give wrong features, so start a fresh window.
    if (consecutiveReadFailures >= 20) {
      samplesInWindow = 0;
    }

    // Try to bring the sensor back, and restart the ESP32 as a last resort.
    if (consecutiveReadFailures % 100 == 0) {
      Serial.println("Trying to restart the sensor...");
      sensorBegin();
    }

    if (consecutiveReadFailures >= 1000) {
      Serial.println("Sensor not responding. Restarting.");
      ESP.restart();
    }

    return;
  }

  consecutiveReadFailures = 0;

  if (DEBUG_PRINT_READINGS && now - lastDebugPrintTime >= 500UL) {
    lastDebugPrintTime = now;
    Serial.printf(
        "AX: %.2f | AY: %.2f | AZ: %.2f | GX: %.2f | GY: %.2f | GZ: %.2f\n",
        sample.values[0], sample.values[1], sample.values[2],
        sample.values[3], sample.values[4], sample.values[5]);
  }

  // Send one raw reading per second (every SAMPLE_RATE_HZ samples).
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