#pragma once

#include <Arduino.h>

bool SensorManager_Begin(bool wakeFromSleep);
void SensorManager_Read();
bool SensorManager_ReadBlocking(unsigned long timeoutMs = 10000);
bool SensorManager_IsInitialized();
float SensorManager_GetTemperature();
float SensorManager_GetHumidity();
uint16_t SensorManager_GetCO2();
