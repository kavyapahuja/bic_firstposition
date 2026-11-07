#pragma once

// One MPU6050 reading:
// values[0..2] = ax, ay, az in g
// values[3..5] = gx, gy, gz in degrees per second
struct SensorSample {
  float values[6];
};

// Start the sensor. Returns false if setup fails.
bool sensorBegin();

// Read one sample. Returns false if the reading fails.
bool sensorRead(SensorSample &sample);