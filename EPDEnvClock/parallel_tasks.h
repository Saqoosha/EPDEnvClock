#pragma once

#include <Arduino.h>
#include "network_manager.h"

// Results from parallel WiFi/NTP and Sensor tasks
struct ParallelTaskResults {
  // WiFi/NTP results
  bool wifiConnected = false;
  bool ntpSynced = false;
  unsigned long wifiConnectTime = 0;
  unsigned long ntpSyncTime = 0;

  // Sensor results
  bool sensorInitialized = false;
  bool sensorReady = false;
};

// Start parallel tasks for WiFi/NTP sync and sensor reading
// - wakeFromSleep: true if waking from deep sleep (affects sensor init)
// - needWifiSync: true if WiFi/NTP sync is needed (otherwise only sensor task runs)
void ParallelTasks_StartWiFiAndSensor(bool wakeFromSleep, bool needWifiSync);

// Wait for both tasks to complete
// Returns true if both completed, false on timeout
bool ParallelTasks_WaitForCompletion(unsigned long timeoutMs);

// Get results from parallel tasks
ParallelTaskResults& ParallelTasks_GetResults();

// Get network state (for display updates)
NetworkState& ParallelTasks_GetNetworkState();
