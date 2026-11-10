
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "sensor.h"

namespace {

constexpr uint8_t MPU_ADDR = 0x68;

bool sensorReady = false;
uint8_t chipID = 0;

// Write a value to a sensor register
bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Read one or more sensor registers
bool readRegisters(uint8_t reg, uint8_t *buffer, uint8_t length) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t received = Wire.requestFrom(MPU_ADDR, length);

  if (received != length) {
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    buffer[i] = Wire.read();
  }

  return true;
}

// Combine two bytes into a signed 16-bit value
int16_t readInt16(uint8_t highByte, uint8_t lowByte) {
  return (int16_t)(((uint16_t)highByte << 8) | lowByte);
}

} // namespace


bool sensorBegin() {

  sensorReady = false;

  if (I2C_SDA_PIN < 0 || I2C_SCL_PIN < 0) {
    Serial.println("Sensor pins are not set in config.h");
    return false;
  }

  // Start I2C using pins from config.h
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  delay(100);

  Serial.println("Detecting MPU sensor...");

  // Read WHO_AM_I register
  if (!readRegisters(0x75, &chipID, 1)) {
    Serial.println("Could not communicate with MPU sensor");
    return false;
  }

  Serial.print("WHO_AM_I = 0x");
  Serial.println(chipID, HEX);

  if (chipID == 0x68) {
    Serial.println("MPU6050 detected");
  }
  else if (chipID == 0x70) {
    Serial.println("MPU6500 detected");
  }
  else {
    Serial.println("Unsupported MPU chip");
    return false;
  }

  // Reset sensor
  if (!writeRegister(0x6B, 0x80)) {
    return false;
  }

  delay(100);

  // Wake up sensor and select clock source
  if (!writeRegister(0x6B, 0x01)) {
    return false;
  }

  delay(100);

  // Enable all accelerometer and gyroscope axes
  if (!writeRegister(0x6C, 0x00)) {
    return false;
  }

  // Gyroscope filter configuration
  if (!writeRegister(0x1A, 0x01)) {
    return false;
  }

  // Internal sample rate: 100 Hz
  if (!writeRegister(0x19, 0x09)) {
    return false;
  }

  // Gyroscope range: +/- 500 degrees per second
  if (!writeRegister(0x1B, 0x08)) {
    return false;
  }

  // Accelerometer range: +/- 8g
  if (!writeRegister(0x1C, 0x10)) {
    return false;
  }

  // MPU6500 has a separate accelerometer filter register
  if (chipID == 0x70) {
    if (!writeRegister(0x1D, 0x01)) {
      return false;
    }
  }

  delay(100);

  sensorReady = true;

  Serial.println("MPU sensor ready");
  Serial.println("Accelerometer range: +/- 8g");
  Serial.println("Gyroscope range: +/- 500 dps");
  Serial.println("DWINETRA sensor initialized successfully");

  return true;
}


bool sensorRead(SensorSample &sample) {

  if (!sensorReady) {
    return false;
  }

  uint8_t data[14];

  // Read accelerometer, temperature and gyroscope
  if (!readRegisters(0x3B, data, 14)) {
    return false;
  }

  int16_t ax = readInt16(data[0], data[1]);
  int16_t ay = readInt16(data[2], data[3]);
  int16_t az = readInt16(data[4], data[5]);

  // data[6] and data[7] contain temperature

  int16_t gx = readInt16(data[8], data[9]);
  int16_t gy = readInt16(data[10], data[11]);
  int16_t gz = readInt16(data[12], data[13]);

  // Convert accelerometer readings to g
  // +/- 8g sensitivity = 4096 LSB/g

  sample.values[0] = ax / 4096.0f;
  sample.values[1] = ay / 4096.0f;
  sample.values[2] = az / 4096.0f;

  // Convert gyroscope readings to degrees per second
  // +/- 500 dps sensitivity = 65.5 LSB/dps

  sample.values[3] = gx / 65.5f;
  sample.values[4] = gy / 65.5f;
  sample.values[5] = gz / 65.5f;
static unsigned long lastPrint = 0;

if (millis() - lastPrint >= 500) {
  lastPrint = millis();

  Serial.printf(
    "AX: %.2f | AY: %.2f | AZ: %.2f | GX: %.2f | GY: %.2f | GZ: %.2f\n",
    sample.values[0],
    sample.values[1],
    sample.values[2],
    sample.values[3],
    sample.values[4],
    sample.values[5]
  );
}
  return true;
}
