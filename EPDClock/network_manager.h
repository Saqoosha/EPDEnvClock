#pragma once

#include <Arduino.h>

struct NetworkState
{
  bool wifiConnected = false;
  bool ntpSynced = false;
  unsigned long wifiConnectTime = 0;
  unsigned long ntpSyncTime = 0;
};

using StatusCallback = void (*)(const char *message);

bool NetworkManager_ConnectWiFi(NetworkState &state, StatusCallback statusCallback);
bool NetworkManager_SyncNtp(NetworkState &state, StatusCallback statusCallback);
bool NetworkManager_CheckNtpResync(NetworkState &state, unsigned long intervalMs, StatusCallback statusCallback);
void NetworkManager_UpdateConnectionState(NetworkState &state);
bool NetworkManager_SetupTimeFromRTC(); // Setup timezone and restore time from RTC when WiFi/NTP fails
