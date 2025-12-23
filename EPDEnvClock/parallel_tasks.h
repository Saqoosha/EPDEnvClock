#pragma once

#include <Arduino.h>
#include "network_manager.h"

// Results from parallel WiFi/NTP and Sensor tasks
struct ParallelTaskResults {
  // WiFi/NTP results
  bool wifiConnected = false;
  bool ntpSynced = false;           // True if system clock was updated via NTP
  bool driftMeasured = false;       // True if drift was successfully measured
  int32_t ntpDriftMs = 0;           // Measured drift in ms (NTP - system), valid only if driftMeasured=true
  int64_t cumulativeCompMs = 0;     // Cumulative compensation applied before NTP sync (for logging)
  unsigned long wifiConnectTime = 0;
  unsigned long ntpSyncTime = 0;

  // Sensor results
  bool sensorInitialized = false;
  bool sensorReady = false;
};

// Start parallel tasks for WiFi/NTP sync and sensor reading
// - wakeFromSleep: true if waking from deep sleep (affects sensor init)
// - needWifiSync: true if full NTP sync is needed (sets system clock)
// - measureDriftOnly: true to measure drift without setting system clock (when needWifiSync=false but WiFi available)
void ParallelTasks_StartWiFiAndSensor(bool wakeFromSleep, bool needWifiSync, bool measureDriftOnly = false);

// Wait for both tasks to complete
// Returns true if both completed, false on timeout
bool ParallelTasks_WaitForCompletion(unsigned long timeoutMs);

// Get results from parallel tasks
ParallelTaskResults& ParallelTasks_GetResults();

// Get network state (for display updates)
NetworkState& ParallelTasks_GetNetworkState();
