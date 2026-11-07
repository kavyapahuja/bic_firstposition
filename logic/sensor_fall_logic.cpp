#include "sensor_fall_logic.h"
#include "../hardware/config.h"
#include "../hardware/model.h"

bool sensorFallDetected(int predictedClass, float peakAccelerationG) {
  if (predictedClass != FD_FALL_CLASS) {
    return false;
  }

  if (peakAccelerationG < FALL_IMPACT_THRESHOLD_G) {
    return false;
  }

  return true;
}