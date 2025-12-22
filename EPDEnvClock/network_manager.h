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

// Measure NTP drift without setting system clock
// Returns drift in milliseconds (NTP time - system time), or INT32_MIN on failure
// Positive value = system clock is behind NTP (slow)
// Negative value = system clock is ahead of NTP (fast)
//
// TEMPORARY DIAGNOSTIC FEATURE (Dec 2025):
// This function is used to measure RTC drift every boot for debugging/analysis.
// When enabled, WiFi connects every boot (~50% more battery consumption).
// To disable: change measureDriftOnly logic in EPDEnvClock.ino
// See docs/RTC_DEEP_SLEEP.md for details.
int32_t NetworkManager_MeasureNtpDrift();

bool NetworkManager_SendBatchData(const String &payload);
