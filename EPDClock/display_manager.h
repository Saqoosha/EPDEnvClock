// Copyright (c) 2024
#pragma once

#include <Arduino.h>
#include <time.h>

#include "network_manager.h"

void DisplayManager_Init();
void DisplayManager_DrawSetupStatus(const char *message);
bool DisplayManager_UpdateDisplay(const NetworkState &networkState, bool forceUpdate = false);
uint8_t *DisplayManager_GetFrameBuffer();
