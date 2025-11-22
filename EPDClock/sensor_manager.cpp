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

bool SensorManager_Begin(bool wakeFromSleep)
{
  // Serial.println("Initializing SDC41 sensor...");

  uint16_t error;
  char errorMessage[256];

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // Set I2C frequency to 100kHz (Standard Mode)
  delay(100);            // Wait a bit for I2C bus to stabilize

  scd4x.begin(Wire, SCD4X_I2C_ADDRESS);

  if (wakeFromSleep)
  {
    // Try to check if sensor is running. Retry a few times as I2C might be busy.
    bool isDataReady = false;
    const int maxRetries = 3;

    for (int i = 0; i < maxRetries; i++)
    {
      error = scd4x.getDataReadyStatus(isDataReady);
      if (error)
      {
        Serial.print("Check DataReadyStatus attempt ");
        Serial.print(i + 1);
        Serial.print(" failed: ");
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.println(errorMessage);

        if (i < maxRetries - 1)
          delay(200); // Wait before retry
      }
      else
      {
        // Success!
        break;
      }
    }

    if (!error && isDataReady)
    {
      Serial.println("SDC41 is already running and data is ready. Skipping initialization.");
      sensorInitialized = true;
      return true;
    }
    else if (!error)
    {
      Serial.println("SDC41 is running but data NOT ready (or just read).");
      Serial.println("Assuming sensor is running in periodic mode. Skipping full init.");
      sensorInitialized = true;
      return true;
    }
    else
    {
      Serial.println("SDC41 failed to respond after retries. Performing full initialization.");
    }
  }
  else
  {
    Serial.println("Cold boot detected. Performing full sensor initialization.");
  }

  // If we get here, either the sensor is stopped, or just started and has no data.
  // We'll proceed with full initialization.
  Serial.println("Initializing SDC41 sensor (full init)...");

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

  // Set temperature offset to 4.0°C
  Serial.println("Setting temperature offset to 4.0°C...");
  error = scd4x.setTemperatureOffset(4.0f);
  if (error)
  {
    Serial.print("Warning: Failed to set temperature offset: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
  }
  else
  {
    Serial.println("Temperature offset set to 4.0°C successfully.");

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

  // Waiting 5 seconds is needed for the very first measurement after power up.
  // Since we deep sleep and the sensor continues running, this delay is typically
  // only needed on cold boot. However, we keep it for all initialization to ensure
  // data validity and avoid race conditions.
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

  error = scd4x.getDataReadyStatus(isDataReady);
  if (error)
  {
    Serial.print("[Sensor] Error getDataReadyStatus: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return;
  }

  if (!isDataReady)
  {
    return;
  }

  uint16_t co2;
  float temperature;
  float humidity;

  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("[Sensor] Error readMeasurement: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
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

  unsigned long totalStartTime = millis();
  unsigned long startTime = millis();
  bool isDataReady = false;
  uint16_t error;
  char errorMessage[256];

  Serial.print("[Sensor] ReadBlocking: Starting wait (timeout=");
  Serial.print(timeoutMs);
  Serial.println("ms)");

  // Wait for data to be ready
  unsigned long waitStartTime = millis();
  int checkCount = 0;
  while (!isDataReady && (millis() - startTime < timeoutMs))
  {
    checkCount++;
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

  unsigned long waitTime = millis() - waitStartTime;
  Serial.print("[Sensor] ReadBlocking: Data ready after ");
  Serial.print(waitTime);
  Serial.print("ms (checked ");
  Serial.print(checkCount);
  Serial.println(" times)");

  if (!isDataReady)
  {
    Serial.println("[Sensor] Timeout waiting for data ready");
    return false;
  }

  // Read measurement
  uint16_t co2;
  float temperature;
  float humidity;

  unsigned long readStartTime = millis();
  error = scd4x.readMeasurement(co2, temperature, humidity);
  unsigned long readTime = millis() - readStartTime;

  if (error)
  {
    Serial.print("[Sensor] Error readMeasurement: ");
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return false;
  }

  unsigned long totalTime = millis() - totalStartTime;
  Serial.print("[Sensor] CO2: ");
  Serial.print(co2);
  Serial.print(" ppm, T: ");
  Serial.print(temperature);
  Serial.print(" °C, H: ");
  Serial.print(humidity);
  Serial.print(" %RH");
  Serial.print(" | Total time: ");
  Serial.print(totalTime);
  Serial.print("ms (wait: ");
  Serial.print(waitTime);
  Serial.print("ms, read: ");
  Serial.print(readTime);
  Serial.println("ms)");

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
