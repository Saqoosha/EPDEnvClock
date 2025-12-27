#include "parallel_tasks.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "network_manager.h"
#include "sensor_manager.h"
#include "deep_sleep_manager.h"
#include "logger.h"

namespace {

// Event group bits
constexpr EventBits_t WIFI_TASK_DONE_BIT = (1 << 0);
constexpr EventBits_t SENSOR_TASK_DONE_BIT = (1 << 1);
constexpr EventBits_t ALL_TASKS_DONE = WIFI_TASK_DONE_BIT | SENSOR_TASK_DONE_BIT;

// Task handles
TaskHandle_t wifiTaskHandle = nullptr;
TaskHandle_t sensorTaskHandle = nullptr;

// Event group for synchronization
EventGroupHandle_t taskEventGroup = nullptr;

// Shared results
ParallelTaskResults results;
NetworkState networkState;

// Task parameters
struct WifiTaskParams {
  bool needWifiSync;
  bool measureDriftOnly;
};

struct SensorTaskParams {
  bool wakeFromSleep;
};

WifiTaskParams wifiParams;
SensorTaskParams sensorParams;

// WiFi/NTP task (runs on Core 0 - WiFi stack uses Core 0)
void wifiNtpTask(void* pvParameters) {
  WifiTaskParams* params = static_cast<WifiTaskParams*>(pvParameters);

  LOGI(LogTag::NETWORK, "WiFi/NTP task started on core %d (sync=%d, measureOnly=%d)",
       xPortGetCoreID(), params->needWifiSync, params->measureDriftOnly);

  bool wifiNeeded = params->needWifiSync || params->measureDriftOnly;

  if (wifiNeeded) {
    // Connect WiFi
    if (NetworkManager_ConnectWiFi(networkState, nullptr)) {
      results.wifiConnected = true;
      results.wifiConnectTime = networkState.wifiConnectTime;

      if (params->needWifiSync) {
        // Full NTP sync mode: sync time AND measure drift
        if (NetworkManager_SyncNtp(networkState, nullptr)) {
          results.ntpSynced = true;
          results.ntpSyncTime = networkState.ntpSyncTime;
          // Save cumulative compensation BEFORE MarkNtpSynced resets it
          RTCState &rtcState = DeepSleepManager_GetRTCState();
          const int64_t preResetCompMs = rtcState.cumulativeCompensationMs;

          DeepSleepManager_MarkNtpSynced(); // Calculate drift (may be skipped if RTC time was invalid)

          const bool driftValid = DeepSleepManager_IsLastRtcDriftValid();
          results.driftMeasured = driftValid;
          results.ntpDriftMs = driftValid ? DeepSleepManager_GetLastRtcDriftMs() : 0;
          results.cumulativeCompMs = driftValid ? preResetCompMs : 0;
          Logger_SetNtpSynced(true);
          LOGI(LogTag::NETWORK, "WiFi/NTP sync completed, drift: %d ms, cumulative comp: %lld ms",
               results.ntpDriftMs, results.cumulativeCompMs);
        } else {
          results.ntpSynced = false;
          Logger_SetNtpSynced(false);
          LOGW(LogTag::NETWORK, "NTP sync failed");
          // Setup timezone and restore time from RTC
          NetworkManager_SetupTimeFromRTC();
        }
      } else {
        // Measure drift only mode: measure drift WITHOUT setting system clock
        int32_t driftMs = NetworkManager_MeasureNtpDrift();
        if (driftMs != INT32_MIN) {
          results.driftMeasured = true;
          results.ntpDriftMs = driftMs;
          // Drift details already logged by NetworkManager_MeasureNtpDrift()
        } else {
          results.driftMeasured = false;
          LOGW(LogTag::NETWORK, "NTP drift measurement failed");
        }
        results.ntpSynced = false;  // System clock was NOT updated
        Logger_SetNtpSynced(true);  // But we still have valid RTC time
      }
    } else {
      results.wifiConnected = false;
      results.ntpSynced = false;
      results.driftMeasured = false;
      LOGW(LogTag::NETWORK, "WiFi connection failed");
      // Setup timezone and restore time from RTC
      NetworkManager_SetupTimeFromRTC();
    }
  } else {
    // WiFi not needed, just mark as done
    results.wifiConnected = false;
    results.ntpSynced = true;  // Assume still synced from RTC
    results.driftMeasured = false;
    Logger_SetNtpSynced(true);
    LOGI(LogTag::NETWORK, "WiFi sync skipped (using RTC time)");
  }

  LOGI(LogTag::NETWORK, "WiFi/NTP task completed");

  // Signal completion
  xEventGroupSetBits(taskEventGroup, WIFI_TASK_DONE_BIT);

  // Delete task
  wifiTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

// Sensor task (runs on Core 1)
void sensorTask(void* pvParameters) {
  SensorTaskParams* params = static_cast<SensorTaskParams*>(pvParameters);

  LOGI(LogTag::SENSOR, "Sensor task started on core %d", xPortGetCoreID());

  // Initialize sensor
  if (SensorManager_Begin(params->wakeFromSleep)) {
    results.sensorInitialized = true;
    LOGI(LogTag::SENSOR, "Sensor initialized");

    // Read sensor (5 second measurement)
    // Always use delay() mode (keepWifiAlive=true) since we're running parallel with WiFi
    if (SensorManager_ReadBlocking(6000, true)) {
      results.sensorReady = true;
      LOGI(LogTag::SENSOR, "Sensor reading completed: T=%.1f, H=%.1f, CO2=%d",
           SensorManager_GetTemperature(),
           SensorManager_GetHumidity(),
           SensorManager_GetCO2());
    } else {
      results.sensorReady = false;
      LOGW(LogTag::SENSOR, "Sensor reading failed");
    }
  } else {
    results.sensorInitialized = false;
    results.sensorReady = false;
    LOGW(LogTag::SENSOR, "Sensor initialization failed");
  }

  LOGI(LogTag::SENSOR, "Sensor task completed");

  // Signal completion
  xEventGroupSetBits(taskEventGroup, SENSOR_TASK_DONE_BIT);

  // Delete task
  sensorTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

void ParallelTasks_StartWiFiAndSensor(bool wakeFromSleep, bool needWifiSync, bool measureDriftOnly) {
  LOGI(LogTag::SETUP, "Starting parallel tasks (wakeFromSleep=%d, needWifiSync=%d, measureDriftOnly=%d)",
       wakeFromSleep, needWifiSync, measureDriftOnly);

  // Reset results
  results = ParallelTaskResults();
  networkState = NetworkState();

  // Create event group if not exists
  if (taskEventGroup == nullptr) {
    taskEventGroup = xEventGroupCreate();
  } else {
    // Clear any previous bits
    xEventGroupClearBits(taskEventGroup, ALL_TASKS_DONE);
  }

  // Store parameters
  wifiParams.needWifiSync = needWifiSync;
  wifiParams.measureDriftOnly = measureDriftOnly;
  sensorParams.wakeFromSleep = wakeFromSleep;

  // Create WiFi/NTP task on Core 0 (WiFi stack runs on Core 0)
  // Stack size: 8KB for WiFi operations
  BaseType_t wifiResult = xTaskCreatePinnedToCore(
    wifiNtpTask,
    "WiFiNTP",
    8192,
    &wifiParams,
    1,  // Priority
    &wifiTaskHandle,
    0   // Core 0
  );

  if (wifiResult != pdPASS) {
    LOGE(LogTag::SETUP, "Failed to create WiFi task");
    xEventGroupSetBits(taskEventGroup, WIFI_TASK_DONE_BIT);
  }

  // Create Sensor task on Core 1
  // Stack size: 4KB for sensor operations
  BaseType_t sensorResult = xTaskCreatePinnedToCore(
    sensorTask,
    "Sensor",
    4096,
    &sensorParams,
    1,  // Priority
    &sensorTaskHandle,
    1   // Core 1
  );

  if (sensorResult != pdPASS) {
    LOGE(LogTag::SETUP, "Failed to create Sensor task");
    xEventGroupSetBits(taskEventGroup, SENSOR_TASK_DONE_BIT);
  }

  LOGI(LogTag::SETUP, "Parallel tasks started");
}

bool ParallelTasks_WaitForCompletion(unsigned long timeoutMs) {
  LOGI(LogTag::SETUP, "Waiting for parallel tasks to complete (timeout: %lu ms)", timeoutMs);

  unsigned long startTime = millis();

  // Wait for both tasks to complete
  EventBits_t bits = xEventGroupWaitBits(
    taskEventGroup,
    ALL_TASKS_DONE,
    pdTRUE,   // Clear bits on exit
    pdTRUE,   // Wait for all bits
    pdMS_TO_TICKS(timeoutMs)
  );

  unsigned long elapsed = millis() - startTime;

  bool allDone = (bits & ALL_TASKS_DONE) == ALL_TASKS_DONE;

  if (allDone) {
    LOGI(LogTag::SETUP, "All parallel tasks completed in %lu ms", elapsed);
  } else {
    LOGW(LogTag::SETUP, "Parallel tasks timeout after %lu ms (bits: 0x%02X)", elapsed, bits);

    // Force delete any stuck tasks
    if (wifiTaskHandle != nullptr) {
      LOGW(LogTag::SETUP, "Deleting stuck WiFi task");
      vTaskDelete(wifiTaskHandle);
      wifiTaskHandle = nullptr;
    }
    if (sensorTaskHandle != nullptr) {
      LOGW(LogTag::SETUP, "Deleting stuck Sensor task");
      vTaskDelete(sensorTaskHandle);
      sensorTaskHandle = nullptr;
    }
  }

  return allDone;
}

ParallelTaskResults& ParallelTasks_GetResults() {
  return results;
}

NetworkState& ParallelTasks_GetNetworkState() {
  return networkState;
}
