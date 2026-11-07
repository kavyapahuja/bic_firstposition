#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

#include "config.h"
#include "sensor.h"

namespace {
Adafruit_MPU6050 mpu;
bool sensorReady = false;
}

bool sensorBegin() {
  // Pin numbers must be filled in config.h after the board and wiring are confirmed.
  if (I2C_SDA_PIN < 0 || I2C_SCL_PIN < 0) {
    Serial.println("Sensor pins are not set in config.h");
    return false;
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!mpu.begin(MPU6050_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("Could not find the MPU6050");
    return false;
  }

  // These are the settings used by the reference sensor pipeline.
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);

  sensorReady = true;
  Serial.println("MPU6050 ready");
  return true;
}

bool sensorRead(SensorSample &sample) {
  if (!sensorReady) {
    return false;
  }

  sensors_event_t acceleration;
  sensors_event_t gyroscope;
  sensors_event_t temperature;
  mpu.getEvent(&acceleration, &gyroscope, &temperature);

  // Store values in the order expected by the model:
  // ax, ay, az in g, followed by gx, gy, gz in degrees per second.
  sample.values[0] = acceleration.acceleration.x / GRAVITY_MS2;
  sample.values[1] = acceleration.acceleration.y / GRAVITY_MS2;
  sample.values[2] = acceleration.acceleration.z / GRAVITY_MS2;

  sample.values[3] = gyroscope.gyro.x * RAD_TO_DEG;
  sample.values[4] = gyroscope.gyro.y * RAD_TO_DEG;
  sample.values[5] = gyroscope.gyro.z * RAD_TO_DEG;

  return true;
}