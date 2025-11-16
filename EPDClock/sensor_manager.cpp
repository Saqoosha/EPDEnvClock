#include "sensor_manager.h"

#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

namespace
{
constexpr uint8_t I2C_SDA_PIN = 38;
constexpr uint8_t I2C_SCL_PIN = 21;
constexpr uint8_t SCD4X_I2C_ADDRESS = 0x62;

SensirionI2cScd4x scd4x;

bool sensorInitialized = false;
} // namespace

bool SensorManager_Begin()
{
  Serial.println("Initializing SDC41 sensor...");

  uint16_t error;
  char errorMessage[256];

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  scd4x.begin(Wire, SCD4X_I2C_ADDRESS);

  error = scd4x.stopPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    sensorInitialized = false;
    return false;
  }

  delay(1000);

  error = scd4x.startPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    sensorInitialized = false;
    return false;
  }

  Serial.println("SDC41 sensor initialized successfully!");
  Serial.println("Waiting for first measurement (5 seconds)...");
  delay(5000);

  sensorInitialized = true;
  return true;
}

void SensorManager_Read()
{
  if (!sensorInitialized)
  {
    return;
  }

  uint16_t error;
  char errorMessage[256];
  bool isDataReady = false;

  unsigned long startTime = millis();
  error = scd4x.getDataReadyStatus(isDataReady);
  if (error)
  {
    Serial.print("[Sensor] Error getDataReadyStatus: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return;
  }

  if (millis() - startTime > 100)
  {
    Serial.println("[Sensor] getDataReadyStatus timeout");
    return;
  }

  if (!isDataReady)
  {
    return;
  }

  uint16_t co2;
  float temperature;
  float humidity;

  startTime = millis();
  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("[Sensor] Error readMeasurement: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return;
  }

  if (millis() - startTime > 200)
  {
    Serial.println("[Sensor] readMeasurement timeout");
    return;
  }

  Serial.print("[Sensor] CO2: ");
  Serial.print(co2);
  Serial.print(" ppm, T: ");
  Serial.print(temperature);
  Serial.print(" °C, H: ");
  Serial.print(humidity);
  Serial.println(" %RH");
}

bool SensorManager_IsInitialized()
{
  return sensorInitialized;
}
