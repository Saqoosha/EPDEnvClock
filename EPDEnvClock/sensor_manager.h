#pragma once

#include <Arduino.h>

constexpr uint8_t SENSOR_I2C_SDA_PIN = 38;
constexpr uint8_t SENSOR_I2C_SCL_PIN = 20;

bool SensorManager_Begin(bool wakeFromSleep);
void SensorManager_Read();
bool SensorManager_ReadBlocking(unsigned long timeoutMs = 10000, bool keepWifiAlive = false);
bool SensorManager_IsInitialized();
float SensorManager_GetTemperature();
float SensorManager_GetHumidity();
uint16_t SensorManager_GetCO2();
// Power down sensor before deep sleep to save power
void SensorManager_PowerDown();
// Wake up sensor from power down mode
void SensorManager_WakeUp();
