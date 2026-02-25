// Copyright (c) 2024
#pragma once

#include <Arduino.h>
#include <time.h>

#include "network_manager.h"

void DisplayManager_Init(bool wakeFromSleep = false);
void DisplayManager_DrawSetupStatus(const char *message);
void DisplayManager_SetStatus(const char *message);
// If overrideTimeinfo is provided, the display will render that time instead of calling getLocalTime().
// This is used to pre-render the *next minute* slightly before the boundary so the visible refresh aligns.
bool DisplayManager_UpdateDisplay(const NetworkState &networkState, bool forceUpdate = false, const struct tm *overrideTimeinfo = nullptr);
void DisplayManager_FullUpdate(const NetworkState &networkState);
uint8_t *DisplayManager_GetFrameBuffer();
// Battery measurement - reads from MAX17048 fuel gauge only
// Returns -1.0f if MAX17048 unavailable or reading invalid (outside 2.0-4.4V range)
float DisplayManager_ReadBatteryVoltage();
// Get battery state of charge in percent (0-100)
float DisplayManager_GetBatteryPercent();
// Get battery charge rate in %/hr (positive=charging, negative=discharging)
float DisplayManager_GetBatteryChargeRate();
// Check if MAX17048 fuel gauge is available
bool DisplayManager_IsFuelGaugeAvailable();
// Global variable to store battery voltage
extern float g_batteryVoltage;
// Global variable to store battery state of charge - linear model (3.4V=0%, 4.2V=100%)
// Used for display - more accurate than MAX17048 below 3.8V
extern float g_batteryPercent;
// Global variable to store MAX17048 reported percent (for reference/logging)
extern float g_batteryMax17048Percent;
// Global variable to store battery charge rate (%/hr)
extern float g_batteryChargeRate;
// Global variable to store charging state (true = charging)
extern bool g_batteryCharging;
