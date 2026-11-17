#pragma once

// ---------------------------------------------------------------------------
// Sampling. The model expects 100 samples per second.
// Each prediction uses 2 seconds of data, then advances by 1 second.
// ---------------------------------------------------------------------------
constexpr int SAMPLE_RATE_HZ = 100;
constexpr int WINDOW_SECONDS = 2;
constexpr int WINDOW_SIZE = SAMPLE_RATE_HZ * WINDOW_SECONDS;  // 200 samples
constexpr int WINDOW_STEP_SIZE = WINDOW_SIZE / 2;             // 100 samples
constexpr unsigned long SAMPLE_PERIOD_MS = 1000UL / SAMPLE_RATE_HZ;

// Convert MPU6050 readings to the units expected by the model.
constexpr float GRAVITY_MS2 = 9.80665f;

// ---------------------------------------------------------------------------
// Fall decision (band worn on the UPPER ARM).
//
// A fall is reported only when the window contains:
//   1. an impact of at least FALL_IMPACT_THRESHOLD_G, then
//   2. the arm settling and staying still for FALL_STILL_SAMPLES samples.
// Arm gestures and fast movement keep moving, so they are rejected.
//
// These are starting values. Watch the "Fall check:" lines in the Serial
// Monitor while you do your pitch gestures and a few simulated falls onto a
// mat, then tune.
// ---------------------------------------------------------------------------
constexpr float FALL_IMPACT_THRESHOLD_G = 2.2f;      // impact peak must reach this
constexpr int FALL_SETTLE_SAMPLES = 30;              // ignore 0.3 s after impact (bounce)
constexpr int FALL_STILL_SAMPLES = 60;               // then require 0.6 s of stillness
constexpr float FALL_STILL_ACC_STD_G = 0.15f;        // max acc-magnitude std while still
constexpr float FALL_STILL_GYRO_MEAN_DPS = 30.0f;    // max mean gyro magnitude while still

// Optional: require a free-fall dip just before the impact.
// Turn on only after you have checked that real falls show it on the arm.
constexpr int FALL_FREEFALL_LOOKBACK_SAMPLES = 50;   // look 0.5 s before impact
constexpr float FALL_FREEFALL_MAX_G = 0.6f;
constexpr bool FALL_REQUIRE_FREEFALL = false;

// The ML model was trained on a lower-back sensor, not an arm. Until it is
// retrained with arm data, the impact + stillness check decides, and the
// model's opinion is only logged. Set true to also require the model to say
// "fall".
constexpr bool FALL_REQUIRE_MODEL = false;

// Avoid repeating alerts for the same event.
constexpr unsigned long ALERT_COOLDOWN_MS = 15000UL;

// A fall message is sent once, then repeated this many times so one lost UDP
// packet cannot lose the alert. The laptop ignores the duplicates.
constexpr int FALL_MESSAGE_REPEATS = 2;
constexpr unsigned long FALL_REPEAT_INTERVAL_MS = 300UL;

// Print one raw reading to the Serial Monitor every 500 ms.
constexpr bool DEBUG_PRINT_READINGS = true;

// ---------------------------------------------------------------------------
// Pins. Make sure these match your wiring.
// ---------------------------------------------------------------------------
constexpr int I2C_SDA_PIN = 22;
constexpr int I2C_SCL_PIN = 23;
constexpr int BUZZER_PIN = -1;