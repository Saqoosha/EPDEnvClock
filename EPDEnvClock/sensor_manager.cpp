#include "sensor_manager.h"

#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include "logger.h"
#include "esp_sleep.h"

namespace
{
// constexpr uint8_t I2C_SDA_PIN = 38; // Moved to header
// constexpr uint8_t I2C_SCL_PIN = 20; // Moved to header
constexpr uint8_t SCD4X_I2C_ADDRESS = 0x62;

SensirionI2cScd4x scd4x;

bool sensorInitialized = false;
float lastTemperature = 0.0f;
float lastHumidity = 0.0f;
uint16_t lastCO2 = 0;
} // namespace

bool SensorManager_Begin(bool wakeFromSleep)
{
  uint16_t error;
  char errorMessage[256];

  Wire.begin(SENSOR_I2C_SDA_PIN, SENSOR_I2C_SCL_PIN);
  Wire.setClock(100000); // Set I2C frequency to 100kHz (Standard Mode)
  delay(100);            // Wait a bit for I2C bus to stabilize

  scd4x.begin(Wire, SCD4X_I2C_ADDRESS);

  if (wakeFromSleep)
  {
    LOGI(LogTag::SENSOR, "Wake from sleep - sensor should be in idle state");
    sensorInitialized = true;
    return true;
  }

  LOGI(LogTag::SENSOR, "Cold boot - performing full initialization");

  // SCD41 defaults to periodic measurement mode on power-up
  // Stop it before switching to single-shot mode
  error = scd4x.stopPeriodicMeasurement();
  if (error)
  {
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGE(LogTag::SENSOR, "stopPeriodicMeasurement failed: %s", errorMessage);
    sensorInitialized = false;
    return false;
  }

  delay(1000); // Wait for sensor to fully stop periodic measurement

  error = scd4x.setTemperatureOffset(4.0f);
  if (error)
  {
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGW(LogTag::SENSOR, "Failed to set temperature offset: %s", errorMessage);
  }
  else
  {
    LOGI(LogTag::SENSOR, "Temperature offset set to 4.0°C successfully.");

    // Read back the temperature offset value
    float tempOffset = 0.0f;
    error = scd4x.getTemperatureOffset(tempOffset);
    if (!error)
    {
      LOGD(LogTag::SENSOR, "Read back temperature offset: %.2f °C", tempOffset);
    }
    else
    {
      errorToString(error, errorMessage, sizeof(errorMessage));
      LOGW(LogTag::SENSOR, "Warning: Failed to read temperature offset: %s", errorMessage);
    }
  }

  LOGI(LogTag::SENSOR, "SCD41 initialized (single-shot mode)");
  sensorInitialized = true;
  return true;
}

void SensorManager_Read()
{
  if (!sensorInitialized)
  {
    return;
  }

  // Non-blocking read - checks if data is ready (for periodic measurement mode)
  // Note: With single-shot mode, use SensorManager_ReadBlocking() instead
  uint16_t error;
  char errorMessage[256];
  bool isDataReady = false;

  error = scd4x.getDataReadyStatus(isDataReady);
  if (error)
  {
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGE(LogTag::SENSOR, "getDataReadyStatus failed: %s", errorMessage);
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
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGE(LogTag::SENSOR, "readMeasurement failed: %s", errorMessage);
    return;
  }

  LOGI(LogTag::SENSOR, "CO2: %d ppm, T: %.2f °C, H: %.2f %%RH", co2, temperature, humidity);

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

  uint16_t error;
  char errorMessage[256];

  // Sensor stays in idle mode (not power-down) for 1-minute intervals
  // This is more efficient than power-cycled mode (~1.5mA vs ~2.6mA)
  // Also enables ASC (Automatic Self-Calibration)

  // Optimization: We assume the sensor is already in idle mode (from previous single-shot or init).
  // Skipping stopPeriodicMeasurement() saves ~600ms of active time (500ms exec + 100ms delay).
  // If the sensor IS busy (unexpected), the single-shot command will fail, and we'll handle it in the error block.

  unsigned long totalStartTime = millis();
  unsigned long measureStartTime = millis();

  // Use single-shot measurement mode (SCD41 only)
  // We manually send the command and use light sleep instead of the blocking delay(5000)
  // measureSingleShot command: 0x219d
  LOGD(LogTag::SENSOR, "Sending single shot command (0x219d)");
  Wire.beginTransmission(SCD4X_I2C_ADDRESS);
  Wire.write(0x21);
  Wire.write(0x9d);
  int16_t result = Wire.endTransmission();
  LOGD(LogTag::SENSOR, "I2C transmission result: %d", result);

  if (result == 0)
  {
    LOGI(LogTag::SENSOR, "Measurement started, light sleeping for 5s...");
    Serial.flush();
    esp_sleep_enable_timer_wakeup(5000000); // 5 seconds
    esp_light_sleep_start();
    LOGD(LogTag::SENSOR, "Woke up from light sleep");
  }

  if (result != 0)
  {
    snprintf(errorMessage, sizeof(errorMessage), "I2C Error: %d", result);
    LOGE(LogTag::SENSOR, "Manual single shot failed: %s", errorMessage);
    LOGW(LogTag::SENSOR, "Falling back to periodic measurement mode");

    LOGD(LogTag::SENSOR, "Starting low power periodic measurement...");
    error = scd4x.startLowPowerPeriodicMeasurement();
    if (error)
    {
      errorToString(error, errorMessage, sizeof(errorMessage));
      LOGE(LogTag::SENSOR, "startLowPowerPeriodicMeasurement failed: %s", errorMessage);
      return false;
    }
    LOGD(LogTag::SENSOR, "Periodic measurement started");

    // Wait for first measurement (5 seconds for low-power periodic mode)
    unsigned long startTime = millis();
    bool isDataReady = false;

    LOGD(LogTag::SENSOR, "Waiting for data ready...");
    while (!isDataReady && (millis() - startTime < timeoutMs))
    {
      error = scd4x.getDataReadyStatus(isDataReady);
      if (error)
      {
        errorToString(error, errorMessage, sizeof(errorMessage));
        LOGE(LogTag::SENSOR, "getDataReadyStatus failed: %s", errorMessage);
        scd4x.stopPeriodicMeasurement();
        return false;
      }

      if (!isDataReady)
      {
        delay(100);
      }
    }

    if (!isDataReady)
    {
      LOGW(LogTag::SENSOR, "Timeout waiting for data ready");
      scd4x.stopPeriodicMeasurement();
      return false;
    }

    scd4x.stopPeriodicMeasurement();
  }

  unsigned long waitTime = millis() - measureStartTime;
  LOGD(LogTag::SENSOR, "Measurement completed (%lums)", waitTime);

  // Read measurement
  uint16_t co2;
  float temperature;
  float humidity;

  LOGD(LogTag::SENSOR, "Reading measurement from sensor...");
  unsigned long readStartTime = millis();
  error = scd4x.readMeasurement(co2, temperature, humidity);
  unsigned long readTime = millis() - readStartTime;
  LOGD(LogTag::SENSOR, "Read measurement result: %d (0=success)", error);

  if (error)
  {
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGE(LogTag::SENSOR, "Error readMeasurement: %s", errorMessage);
    return false;
  }

  unsigned long totalTime = millis() - totalStartTime;
  LOGI(LogTag::SENSOR, "CO2: %d ppm, T: %.2f °C, H: %.2f %%RH | Total time: %lums (measure: %lums, read: %lums)",
       co2, temperature, humidity, totalTime, waitTime, readTime);

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

void SensorManager_PowerDown()
{
  // Note: Not used for 1-minute intervals (idle single-shot is more efficient)
  // Power-down is only beneficial for intervals > 380 seconds (~6 minutes)
  // Kept for future use or longer measurement intervals
  if (!sensorInitialized)
  {
    return;
  }

  scd4x.stopPeriodicMeasurement(); // Ignore error - may not be running
  delay(100);

  uint16_t error = scd4x.powerDown();
  if (error)
  {
    char errorMessage[256];
    errorToString(error, errorMessage, sizeof(errorMessage));
    LOGW(LogTag::SENSOR, "powerDown failed: %s", errorMessage);
  }
  else
  {
    LOGI(LogTag::SENSOR, "Sensor powered down (~18µA)");
  }
}

void SensorManager_WakeUp()
{
  // Note: Not used for 1-minute intervals (sensor stays in idle mode)
  // Kept for future use or power-cycled mode
  if (!sensorInitialized)
  {
    return;
  }

  scd4x.wakeUp(); // Ignore error - sensor may already be awake
  delay(20);      // Wait for sensor to stabilize
}
