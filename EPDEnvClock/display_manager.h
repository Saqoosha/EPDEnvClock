// Copyright (c) 2024
#pragma once

#include <Arduino.h>
#include <time.h>

#include "network_manager.h"

void DisplayManager_Init(bool wakeFromSleep = false);
void DisplayManager_DrawSetupStatus(const char *message);
void DisplayManager_SetStatus(const char *message);
bool DisplayManager_UpdateDisplay(const NetworkState &networkState, bool forceUpdate = false);
void DisplayManager_FullUpdate(const NetworkState &networkState);
// Two-phase update: first show time quickly, then add sensor values after reading
bool DisplayManager_UpdateTimeOnly(const NetworkState &networkState, bool forceUpdate = false);
void DisplayManager_UpdateSensorOnly(const NetworkState &networkState);
uint8_t *DisplayManager_GetFrameBuffer();
// Battery measurement - reads from MAX17048 fuel gauge (or ADC fallback)
float DisplayManager_ReadBatteryVoltage();
// Get battery state of charge in percent (0-100)
float DisplayManager_GetBatteryPercent();
// Get battery charge rate in %/hr (positive=charging, negative=discharging)
float DisplayManager_GetBatteryChargeRate();
// Check if MAX17048 fuel gauge is available
bool DisplayManager_IsFuelGaugeAvailable();
// Global variable to store battery voltage
extern float g_batteryVoltage;
// Global variable to store battery state of charge (percent)
extern float g_batteryPercent;
// Global variable to store battery charge rate (%/hr)
extern float g_batteryChargeRate;
// Global variable to store charging state (true = charging)
extern bool g_batteryCharging;
