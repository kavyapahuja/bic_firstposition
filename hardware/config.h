#pragma once

// The model expects 100 samples per second.
// Each prediction uses 2 seconds of data, then advances by 1 second.
constexpr int SAMPLE_RATE_HZ = 100;
constexpr int WINDOW_SECONDS = 2;
constexpr int WINDOW_SIZE = SAMPLE_RATE_HZ * WINDOW_SECONDS;  // 200 samples
constexpr int WINDOW_STEP_SIZE = WINDOW_SIZE / 2;             // 100 samples
constexpr unsigned long SAMPLE_PERIOD_MS = 1000UL / SAMPLE_RATE_HZ;

// Convert MPU6050 readings to the units expected by the model.
constexpr float GRAVITY_MS2 = 9.80665f;
constexpr float RAD_TO_DEG = 57.29578f;

// Keep the reference impact threshold for now.
// TODO: Review after testing with the actual sensor.
constexpr float FALL_IMPACT_THRESHOLD_G = 1.7f;

// Avoid repeating alerts for the same event.
constexpr unsigned long ALERT_COOLDOWN_MS = 15000UL;

// TODO: Fill these in after confirming the ESP32 pins and wiring.
constexpr int I2C_SDA_PIN = -1;
constexpr int I2C_SCL_PIN = -1;
constexpr int BUZZER_PIN = -1;