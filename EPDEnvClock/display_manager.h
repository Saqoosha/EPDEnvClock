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
uint8_t *DisplayManager_GetFrameBuffer();
// Battery voltage measurement - should be called early in setup() before WiFi/sensor operations
float DisplayManager_ReadBatteryVoltage();
// Get battery raw ADC value (read after DisplayManager_ReadBatteryVoltage() was called)
int DisplayManager_GetBatteryRawADC();
// Global variable to store battery voltage (measured early in setup, before WiFi/sensor operations)
extern float g_batteryVoltage;
// Global variable to store battery raw ADC value
extern int g_batteryRawADC;
