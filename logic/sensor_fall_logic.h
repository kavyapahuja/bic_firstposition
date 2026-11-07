#pragma once

// Returns true when the model predicts a fall and peak acceleration
// reaches the threshold set in hardware/config.h.
bool sensorFallDetected(int predictedClass, float peakAccelerationG);