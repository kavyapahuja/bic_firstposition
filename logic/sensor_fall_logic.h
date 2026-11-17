#pragma once

// Result of checking one 2-second window for a fall.
struct SensorFallResult {
  bool fall;               // final decision
  bool modelSaysFall;      // what the ML model said
  bool gatePassed;         // impact + stillness pattern found
  int impactIndex;         // sample index of the impact, -1 if none
  float impactG;           // size of the impact
  float preMinG;           // lowest acceleration just before the impact
  float stillAccStdG;      // movement after the impact (acceleration)
  float stillGyroMeanDps;  // movement after the impact (rotation)
  const char *reason;      // short text explaining the decision
};

// samples: numSamples rows of {ax, ay, az, gx, gy, gz} in g and deg/s.
// Thresholds are in hardware/config.h.
SensorFallResult sensorFallCheck(bool modelSaysFall,
                                 const float *samples,
                                 int numSamples);