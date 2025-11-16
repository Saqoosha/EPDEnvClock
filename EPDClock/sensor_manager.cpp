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
float lastTemperature = 0.0f;
float lastHumidity = 0.0f;
uint16_t lastCO2 = 0;
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

  // Set temperature offset to 0°C
  // Serial.println("Setting temperature offset to 0°C...");
  // error = scd4x.setTemperatureOffset(0.0f);
  // if (error)
  // {
  //   Serial.print("Warning: Failed to set temperature offset: ");
  //   errorToString(error, errorMessage, sizeof(errorMessage));
  //   Serial.println(errorMessage);
  // }
  // else
  {
    // Serial.println("Temperature offset set to 0°C successfully.");

    // Read back the temperature offset value
    float tempOffset = 0.0f;
    error = scd4x.getTemperatureOffset(tempOffset);
    if (!error)
    {
      Serial.print("Read back temperature offset: ");
      Serial.print(tempOffset);
      Serial.println(" °C");
    }
    else
    {
      Serial.print("Warning: Failed to read temperature offset: ");
      errorToString(error, errorMessage, sizeof(errorMessage));
      Serial.println(errorMessage);
    }
  }

  // Start low power periodic measurement mode
  // This mode reduces self-heating and improves temperature accuracy
  error = scd4x.startLowPowerPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startLowPowerPeriodicMeasurement(): ");
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

  lastTemperature = temperature;
  lastHumidity = humidity;
  lastCO2 = co2;
}

bool SensorManager_ReadBlocking(unsigned long timeoutMs)
{
  if (!sensorInitialized)
  {
    return false;
  }

  unsigned long startTime = millis();
  bool isDataReady = false;
  uint16_t error;
  char errorMessage[256];

  // Wait for data to be ready
  while (!isDataReady && (millis() - startTime < timeoutMs))
  {
    error = scd4x.getDataReadyStatus(isDataReady);
    if (error)
    {
      Serial.print("[Sensor] Error getDataReadyStatus: ");
      errorToString(error, errorMessage, sizeof(errorMessage));
      Serial.println(errorMessage);
      return false;
    }

    if (!isDataReady)
    {
      delay(100); // Wait 100ms before checking again
    }
  }

  if (!isDataReady)
  {
    Serial.println("[Sensor] Timeout waiting for data ready");
    return false;
  }

  // Read measurement
  uint16_t co2;
  float temperature;
  float humidity;

  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("[Sensor] Error readMeasurement: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return false;
  }

  Serial.print("[Sensor] CO2: ");
  Serial.print(co2);
  Serial.print(" ppm, T: ");
  Serial.print(temperature);
  Serial.print(" °C, H: ");
  Serial.print(humidity);
  Serial.println(" %RH");

  lastTemperature = temperature;
  lastHumidity = humidity;
  lastCO2 = co2;
  return true;
}

bool SensorManager_IsInitialized()
{
  return sensorInitialized;
}

float SensorManager_GetTemperature()
{
  return lastTemperature;
}

float SensorManager_GetHumidity()
{
  return lastHumidity;
}

uint16_t SensorManager_GetCO2()
{
  return lastCO2;
}
