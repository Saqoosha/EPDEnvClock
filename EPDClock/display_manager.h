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
