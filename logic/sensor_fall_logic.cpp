#include <math.h>

#include "sensor_fall_logic.h"
#include "../hardware/config.h"
#include "../hardware/features.h"  // FD_NUM_AXES

// The window moves forward by WINDOW_STEP_SIZE samples. Every impact lands in
// the first half of exactly one window, and that window always has enough
// samples after it to check stillness. This only holds if the search area is
// at least one step long.
static_assert(
    WINDOW_SIZE - FALL_SETTLE_SAMPLES - FALL_STILL_SAMPLES >= WINDOW_STEP_SIZE,
    "Settle + still samples are too long for the window step");

namespace {

float accMagnitude(const float *s) {
  return sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
}

float gyroMagnitude(const float *s) {
  return sqrtf(s[3] * s[3] + s[4] * s[4] + s[5] * s[5]);
}

}  // namespace

SensorFallResult sensorFallCheck(bool modelSaysFall,
                                 const float *samples,
                                 int numSamples) {
  SensorFallResult r = {false, modelSaysFall, false, -1,
                        0.0f,  99.0f,         0.0f,  0.0f,
                        "no impact"};

  // Only look at impacts that still have settle + stillness samples after them.
  const int searchEnd = numSamples - FALL_SETTLE_SAMPLES - FALL_STILL_SAMPLES;
  if (searchEnd <= 0) {
    r.reason = "window too short";
    return r;
  }

  // 1) Strongest impact in the searchable part of the window.
  float best = 0.0f;
  int bestIndex = -1;
  for (int i = 0; i < searchEnd; i++) {
    const float m = accMagnitude(samples + i * FD_NUM_AXES);
    if (m > best) {
      best = m;
      bestIndex = i;
    }
  }

  r.impactIndex = bestIndex;
  r.impactG = best;

  if (bestIndex < 0 || best < FALL_IMPACT_THRESHOLD_G) {
    r.reason = "impact too small";
    return r;
  }

  // 2) Lowest acceleration just before the impact (free-fall dip).
  int preStart = bestIndex - FALL_FREEFALL_LOOKBACK_SAMPLES;
  if (preStart < 0) {
    preStart = 0;
  }
  for (int i = preStart; i < bestIndex; i++) {
    const float m = accMagnitude(samples + i * FD_NUM_AXES);
    if (m < r.preMinG) {
      r.preMinG = m;
    }
  }

  if (FALL_REQUIRE_FREEFALL && r.preMinG > FALL_FREEFALL_MAX_G) {
    r.reason = "no free-fall before impact";
    return r;
  }

  // 3) Stillness after the impact has settled.
  const int stillStart = bestIndex + FALL_SETTLE_SAMPLES;
  float accSum = 0.0f;
  float gyroSum = 0.0f;

  for (int i = stillStart; i < stillStart + FALL_STILL_SAMPLES; i++) {
    const float *s = samples + i * FD_NUM_AXES;
    accSum += accMagnitude(s);
    gyroSum += gyroMagnitude(s);
  }

  const float accMean = accSum / (float)FALL_STILL_SAMPLES;
  float varianceSum = 0.0f;

  for (int i = stillStart; i < stillStart + FALL_STILL_SAMPLES; i++) {
    const float d = accMagnitude(samples + i * FD_NUM_AXES) - accMean;
    varianceSum += d * d;
  }

  r.stillAccStdG = sqrtf(varianceSum / (float)FALL_STILL_SAMPLES);
  r.stillGyroMeanDps = gyroSum / (float)FALL_STILL_SAMPLES;

  if (r.stillAccStdG > FALL_STILL_ACC_STD_G) {
    r.reason = "still moving after impact (acceleration)";
    return r;
  }

  if (r.stillGyroMeanDps > FALL_STILL_GYRO_MEAN_DPS) {
    r.reason = "still rotating after impact (gyro)";
    return r;
  }

  r.gatePassed = true;

  if (FALL_REQUIRE_MODEL && !modelSaysFall) {
    r.reason = "fall pattern, but model disagrees";
    return r;
  }

  r.fall = true;
  r.reason = "fall pattern";
  return r;
}