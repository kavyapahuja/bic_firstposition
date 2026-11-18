#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "sensor.h"
#include "features.h"
#include "model.h"
#include "wifi_status.h"
#include "../logic/sensor_fall_logic.h"

// =====================================================
// GLOBAL VARIABLES
// =====================================================

static float window[WINDOW_SIZE][FD_NUM_AXES];

static int samplesInWindow = 0;
static unsigned long nextSampleTime = 0;
static unsigned long lastFallMessageTime = 0;
static unsigned long lastDebugPrintTime = 0;

static int samplesSinceReadingMessage = 0;
static int consecutiveReadFailures = 0;

// Pause monitoring for 10 seconds after a confirmed fall.
static constexpr unsigned long FALL_PAUSE_MS = 10000UL;

static unsigned long fallPauseStartTime = 0;
static bool fallPauseActive = false;


// =====================================================
// ML CLASS DECISION SCORE
// =====================================================

static float classDecisionScore(
    const float *rawFeatures,
    int classIndex
) {
  float score = FD_INTERCEPT[classIndex];

  for (int i = 0; i < FD_NUM_FEATURES; i++) {
    const float scaled =
        (rawFeatures[i] - FD_SCALER_MEAN[i]) /
        FD_SCALER_SCALE[i];

    score += scaled * FD_COEF[classIndex][i];
  }

  return score;
}


// =====================================================
// FALL DETECTION
// =====================================================

static void checkWindowForFall() {

  float features[FD_NUM_FEATURES];

  fd_extract_features(
      &window[0][0],
      WINDOW_SIZE,
      features
  );

  // Run the existing ML model.
  float winningScore = 0.0f;

  const int predictedClass =
      fd_predict(features, &winningScore);

  const bool modelSaysFall =
      (predictedClass == FD_FALL_CLASS);

  // Calculate fall-class decision score.
  const float fallScore =
      classDecisionScore(features, FD_FALL_CLASS);

  float highestOtherScore = 0.0f;
  bool foundOtherClass = false;

  for (int classIndex = 0;
       classIndex < FD_NUM_CLASSES;
       classIndex++) {

    if (classIndex == FD_FALL_CLASS) {
      continue;
    }

    const float score =
        classDecisionScore(features, classIndex);

    if (!foundOtherClass || score > highestOtherScore) {
      highestOtherScore = score;
      foundOtherClass = true;
    }
  }

  const float fallMargin =
      fallScore - highestOtherScore;

  const float peakAccelerationG =
      fd_peak_acc_g(&window[0][0], WINDOW_SIZE);

  // No activity classification is printed or transmitted.
  // The ML model still runs internally.


  // ===================================================
  // IMPACT + STILLNESS CHECK
  // ===================================================

  const SensorFallResult result =
      sensorFallCheck(
          modelSaysFall,
          &window[0][0],
          WINDOW_SIZE
      );

  // Keep the fall-check diagnostics available.
  // These can be disabled later if required.

  Serial.printf(
      "Fall check: %s | impact %.2f g | "
      "still acc std %.3f g | "
      "still gyro %.1f dps\n",
      result.reason,
      result.impactG,
      result.stillAccStdG,
      result.stillGyroMeanDps
  );


  // ===================================================
  // ALERT COOLDOWN
  // ===================================================

  const unsigned long now = millis();

  const bool cooldownOver =
      (lastFallMessageTime == 0) ||
      (now - lastFallMessageTime >= ALERT_COOLDOWN_MS);


  // ===================================================
  // CONFIRMED FALL
  // ===================================================

  if (result.fall && cooldownOver) {

    Serial.println();
    Serial.println("================================");
    Serial.println("       DWINETRA FALL DETECTED");
    Serial.println("================================");

    Serial.printf(
        "Impact acceleration: %.2f g\n",
        result.impactG
    );

    // Send fall alert over Wi-Fi.
    wifiSendFall((uint32_t)now, result.impactG);

    lastFallMessageTime = now;

    // Start the 10-second pause.
    fallPauseStartTime = millis();
    fallPauseActive = true;

    Serial.println();
    Serial.println("Monitoring paused for 10 seconds...");
  }
}


// =====================================================
// MOVE SENSOR WINDOW FORWARD
// =====================================================

static void moveWindowForward() {

  memmove(
      window,
      window[WINDOW_STEP_SIZE],
      (WINDOW_SIZE - WINDOW_STEP_SIZE) *
      FD_NUM_AXES *
      sizeof(float)
  );

  samplesInWindow =
      WINDOW_SIZE - WINDOW_STEP_SIZE;
}


// =====================================================
// ESP32 SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println("       DWINETRA STARTING");
  Serial.println("================================");

  // Initialize the MPU sensor.
  if (!sensorBegin()) {

    Serial.println(
        "Sensor setup failed. Check I2C pins and wiring."
    );

    Serial.println("Restarting in 3 seconds...");

    delay(3000);
    ESP.restart();
  }

  // Connect to Wi-Fi.
  wifiStatusBegin();

  Serial.println(
      "Sensor ready. Wi-Fi messages will start after connection."
  );

  nextSampleTime = millis();

  Serial.println("DWINETRA monitoring started.");
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // Keep Wi-Fi services running during the pause.
  wifiStatusService();

  const unsigned long now = millis();


  // ===================================================
  // 10-SECOND PAUSE AFTER FALL
  // ===================================================

  if (fallPauseActive) {

    if (now - fallPauseStartTime < FALL_PAUSE_MS) {
      return;
    }

    // The pause has finished.
    fallPauseActive = false;

    // Discard old samples so the next prediction
    // uses fresh sensor data.
    samplesInWindow = 0;
    samplesSinceReadingMessage = 0;
    consecutiveReadFailures = 0;

    nextSampleTime = now;

    Serial.println();
    Serial.println("================================");
    Serial.println("       MONITORING RESUMED");
    Serial.println("================================");
    Serial.println("10-second pause completed.");
  }


  // ===================================================
  // MAINTAIN 100 Hz SAMPLING
  // ===================================================

  if ((long)(now - nextSampleTime) < 0) {
    return;
  }

  nextSampleTime += SAMPLE_PERIOD_MS;

  if ((long)(now - nextSampleTime) >= 0) {
    nextSampleTime = now + SAMPLE_PERIOD_MS;
  }


  // ===================================================
  // READ MPU SENSOR
  // ===================================================

  SensorSample sample;

  if (!sensorRead(sample)) {

    consecutiveReadFailures++;

    if (
        consecutiveReadFailures == 1 ||
        consecutiveReadFailures % 50 == 0
    ) {
      Serial.println("Could not read the MPU sensor");
    }

    // Reset the sample window after repeated failures.
    if (consecutiveReadFailures >= 20) {
      samplesInWindow = 0;
    }

    // Attempt sensor recovery.
    if (consecutiveReadFailures % 100 == 0) {

      Serial.println("Trying to restart the sensor...");
      sensorBegin();
    }

    // Restart ESP32 after prolonged sensor failure.
    if (consecutiveReadFailures >= 1000) {

      Serial.println("Sensor not responding. Restarting.");
      ESP.restart();
    }

    return;
  }

  consecutiveReadFailures = 0;


  // ===================================================
  // DEBUG RAW SENSOR READINGS
  // ===================================================

  if (
      DEBUG_PRINT_READINGS &&
      now - lastDebugPrintTime >= 500UL
  ) {

    lastDebugPrintTime = now;

    Serial.printf(
        "AX: %.2f | AY: %.2f | AZ: %.2f | "
        "GX: %.2f | GY: %.2f | GZ: %.2f\n",

        sample.values[0],
        sample.values[1],
        sample.values[2],
        sample.values[3],
        sample.values[4],
        sample.values[5]
    );
  }


  // ===================================================
  // SEND RAW SENSOR READINGS OVER WI-FI
  // ===================================================

  samplesSinceReadingMessage++;

  if (samplesSinceReadingMessage >= SAMPLE_RATE_HZ) {

    samplesSinceReadingMessage = 0;
    wifiSendReading(sample);
  }


  // ===================================================
  // ADD SAMPLE TO ML WINDOW
  // ===================================================

  memcpy(
      window[samplesInWindow],
      sample.values,
      sizeof(sample.values)
  );

  samplesInWindow++;


  // ===================================================
  // RUN FALL DETECTION
  // ===================================================

  if (samplesInWindow == WINDOW_SIZE) {

    checkWindowForFall();

    moveWindowForward();
  }
}