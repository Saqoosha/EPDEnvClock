#include <SPI.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>

#include "display_manager.h"
#include "imagebw_export.h"
#include "network_manager.h"
#include "sensor_manager.h"
#include "server_config.h"
#include "EPD_Init.h"
#include "deep_sleep_manager.h"
#include "logger.h"
#include "sensor_logger.h"
#include "parallel_tasks.h"

namespace
{
  constexpr size_t kFrameBufferSize = 27200;
  constexpr unsigned long kNtpSyncInterval = 3600000; // 1 hour
  constexpr float kWifiMinBatteryVoltage = 3.4f;      // Avoid WiFi below this voltage

  // Button pin definitions (from 5.79_key example)
  constexpr int HOME_KEY = 2; // Home button
  constexpr int EXIT_KEY = 1; // Exit button
  constexpr int PRV_KEY = 6;  // Previous button
  constexpr int NEXT_KEY = 4; // Next button
  constexpr int OK_KEY = 5;   // OK/Confirm button

  NetworkState networkState;
  bool sensorInitialized = false;
  bool ntpSyncedThisBoot = false; // Track if NTP synced during THIS boot cycle

#if ENABLE_IMAGEBW_EXPORT
bool enableImageBWExport = true;
#else
bool enableImageBWExport = false;
#endif

void exportFrameBuffer()
{
  if (!enableImageBWExport)
  {
    return;
  }

  ImageBWExporter_Send(DisplayManager_GetFrameBuffer(), kFrameBufferSize);
  DisplayManager_SetStatus("Export Done");
}

void onStatusUpdate(const char *message)
{
  DisplayManager_SetStatus(message);
}

// Note: Sensor initialization now happens in parallel_tasks.cpp

void updateDisplay(bool forceUpdate)
{
  if (DisplayManager_UpdateDisplay(networkState, forceUpdate))
  {
    exportFrameBuffer();
  }
}

bool checkButton(int pin)
{
  // Buttons are active LOW (0 when pressed)
  if (digitalRead(pin) == 0)
  {
    delay(50); // Debounce delay
    if (digitalRead(pin) == 0)
    {
      // Wait for button release
      while (digitalRead(pin) == 0)
      {
        delay(10);
      }
      return true;
    }
  }
  return false;
}

bool checkAnyButton()
{
  return checkButton(HOME_KEY) || checkButton(EXIT_KEY) ||
         checkButton(PRV_KEY) || checkButton(NEXT_KEY) ||
         checkButton(OK_KEY);
}
} // namespace

void setup()
{
  Serial.begin(115200);

  // Release I2C pins hold if they were held during deep sleep
  DeepSleepManager_ReleaseI2CPins();
  // Release EPD pins hold if they were held during deep sleep
  DeepSleepManager_ReleaseEPDPins();

  // Initialize logger (default: DEBUG level, BOTH timestamp mode)
  Logger_Init(LogLevel::DEBUG, TimestampMode::BOTH);

  LOGI(LogTag::SETUP, "=== EPD Clock with SCD41 Sensor ===");

  // Initialize deep sleep manager first (checks if wake from sleep)
  DeepSleepManager_Init();
  bool wakeFromSleep = DeepSleepManager_IsWakeFromSleep();

  // If woke from GPIO (button press), treat as cold boot for full display init
  if (DeepSleepManager_IsWakeFromGPIO())
  {
    LOGI(LogTag::SETUP, "Woke from GPIO - forcing cold boot sequence for full display init");
    wakeFromSleep = false;
  }

  if (wakeFromSleep)
  {
    LOGI(LogTag::SETUP, "Woke from deep sleep");
  }
  else
  {
    LOGI(LogTag::SETUP, "Cold boot (or forced cold boot)");
  }

  randomSeed(analogRead(0));

  // Read battery voltage early (before WiFi/sensor operations)
  // This ensures we measure voltage when battery is in near-idle state (no load)
  // After deep sleep, battery voltage recovers, so measuring early gives more accurate reading
  g_batteryVoltage = DisplayManager_ReadBatteryVoltage();

  // Initialize button pins
  pinMode(HOME_KEY, INPUT_PULLUP);
  pinMode(EXIT_KEY, INPUT_PULLUP);
  pinMode(PRV_KEY, INPUT_PULLUP);
  pinMode(NEXT_KEY, INPUT_PULLUP);
  pinMode(OK_KEY, INPUT_PULLUP);

  // Check if wakeup was from GPIO (button press)
  if (DeepSleepManager_IsWakeFromGPIO())
  {
    int wakeupPin = DeepSleepManager_GetWakeupGPIO();
    LOGI(LogTag::SETUP, "Woke from GPIO (button press on pin %d)", wakeupPin);
  }

  // Initialize display manager with wakeFromSleep flag
  // This determines if we do full init or minimal wake-up
  DisplayManager_Init(wakeFromSleep);

  // === Parallel WiFi/NTP + Sensor Reading ===
  // Run WiFi/NTP sync and sensor reading in parallel using dual cores
  // This reduces startup time from ~18s to ~13s and enables single screen update

  // Determine if WiFi sync is needed
  bool needWifiSync = DeepSleepManager_ShouldSyncWiFiNtp();
  bool skipWifiDueToLowBattery = false;

  if (needWifiSync)
  {
    LOGI("Setup", "WiFi/NTP sync needed (top of hour)");

    // Skip WiFi if battery voltage is low OR invalid (-1.0 means sensor error)
    // Exception: allow WiFi when charging even if voltage is low
    if ((g_batteryVoltage < 0.0f || g_batteryVoltage < kWifiMinBatteryVoltage) && !g_batteryCharging)
    {
      LOGW("Setup", "Skipping WiFi/NTP sync: %s (%.3fV)",
           g_batteryVoltage < 0.0f ? "battery sensor error" : "low battery", g_batteryVoltage);
      skipWifiDueToLowBattery = true;
      needWifiSync = false;

      // Setup timezone and restore time from RTC
      if (NetworkManager_SetupTimeFromRTC())
      {
        LOGI("Setup", "Time restored from RTC after skipping WiFi");
      }
      else
      {
        LOGW("Setup", "Failed to restore time from RTC (WiFi skipped)");
      }
    }
  }
  else
  {
    // WiFi/NTP sync not needed (less than 1 hour since last sync)
    LOGI("Setup", "Skipping WiFi/NTP sync (less than 1 hour since last sync)");
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    LOGI("Setup", "Last sync was %d boots ago", rtcState.bootCount - rtcState.lastNtpSyncBootCount);
  }

  // Show status before starting tasks
  if (needWifiSync)
  {
    DisplayManager_DrawSetupStatus("WiFi + Sensor...");
  }
  else
  {
    DisplayManager_DrawSetupStatus("Reading Sensor...");
  }

  unsigned long taskStartTime = millis();
  bool sensorReady = false;

  if (needWifiSync)
  {
    // === WiFi sync needed: Use parallel processing (dual-core) ===
    // Run WiFi/NTP sync and sensor reading in parallel
    // This reduces startup time from ~18s to ~13s
    LOGI("Setup", "Starting parallel tasks (WiFi + Sensor)");
    ParallelTasks_StartWiFiAndSensor(wakeFromSleep, true);

    // Wait for both tasks to complete (20 second timeout)
    bool parallelSuccess = ParallelTasks_WaitForCompletion(20000);

    // Get results from parallel tasks
    ParallelTaskResults &results = ParallelTasks_GetResults();
    networkState = ParallelTasks_GetNetworkState();
    sensorInitialized = results.sensorInitialized;
    sensorReady = results.sensorReady;

    unsigned long taskDuration = millis() - taskStartTime;
    LOGI("Setup", "Parallel tasks completed in %lu ms (WiFi:%d, NTP:%d, Sensor:%d)",
         taskDuration,
         results.wifiConnected ? 1 : 0,
         results.ntpSynced ? 1 : 0,
         results.sensorReady ? 1 : 0);

    // Track if NTP synced this boot for drift calculation
    if (results.ntpSynced)
    {
      ntpSyncedThisBoot = true;
    }
  }
  else
  {
    // === WiFi sync NOT needed: Use single-core with light_sleep ===
    // This saves power by using light_sleep during sensor measurement wait
    // (~0.8mA vs ~20mA with delay())
    LOGI("Setup", "Starting sensor-only task (with light_sleep)");

    // Setup timezone and restore time from RTC
    if (NetworkManager_SetupTimeFromRTC())
    {
      LOGI("Setup", "Time restored from RTC");
    }
    else
    {
      LOGW("Setup", "Failed to restore time from RTC");
    }

    // No WiFi connection - RTC maintains time
    networkState.wifiConnected = false;
    networkState.ntpSynced = true; // Assume still synced (RTC keeps time)
    Logger_SetNtpSynced(true);

    // Initialize and read sensor (single-core, uses light_sleep)
    if (SensorManager_Begin(wakeFromSleep))
    {
      sensorInitialized = true;
      LOGI(LogTag::SENSOR, "Sensor initialized");

      // Read sensor with light_sleep (keepWifiAlive=false)
      if (SensorManager_ReadBlocking(6000, false))
      {
        sensorReady = true;
        LOGI(LogTag::SENSOR, "Sensor reading completed: T=%.1f, H=%.1f, CO2=%d",
             SensorManager_GetTemperature(),
             SensorManager_GetHumidity(),
             SensorManager_GetCO2());
      }
      else
      {
        LOGW(LogTag::SENSOR, "Sensor reading failed");
      }
    }
    else
    {
      sensorInitialized = false;
      LOGW(LogTag::SENSOR, "Sensor initialization failed");
    }

    unsigned long taskDuration = millis() - taskStartTime;
    LOGI("Setup", "Sensor-only task completed in %lu ms (Sensor:%d)",
         taskDuration, sensorReady ? 1 : 0);
  }

  // Handle case where WiFi was skipped due to low battery
  if (skipWifiDueToLowBattery)
  {
    networkState.wifiConnected = false;
    networkState.ntpSynced = false;
    Logger_SetNtpSynced(false);
  }

  // Initialize sensor logger (after SD card is initialized by DeepSleepManager_Init)
  SensorLogger_Init();

  // === Safety: Wait if minute hasn't changed yet (only after processing) ===
  // This check happens AFTER parallel tasks complete, so processing time has elapsed.
  // If we still haven't reached the next minute, wait briefly.
  // Also capture the time at draw moment for adaptive timing adjustment (ms precision).
  float delayAtDrawTime = 0.0f; // Seconds past minute boundary (with ms precision)
  float waitedSeconds = 0.0f;   // How long we waited for minute to change
  if (wakeFromSleep)
  {
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    struct tm timeinfo;
    struct timeval tv;
    if (getLocalTime(&timeinfo))
    {
      uint8_t currentMinute = timeinfo.tm_min;
      if (currentMinute == rtcState.lastDisplayedMinute)
      {
        // Calculate exact milliseconds until next minute boundary
        gettimeofday(&tv, NULL);
        uint16_t currentMs = tv.tv_usec / 1000;
        uint16_t msUntilNextMinute = (60 - timeinfo.tm_sec) * 1000 - currentMs;
        waitedSeconds = msUntilNextMinute / 1000.0f; // Record wait time for adjustment
        LOGI(LogTag::SETUP, "Still same minute (%d), waiting %d ms for next minute...",
             currentMinute, msUntilNextMinute);
        delay(msUntilNextMinute);
        // Refresh time after waiting
        if (getLocalTime(&timeinfo))
        {
          currentMinute = timeinfo.tm_min;
        }
        LOGI(LogTag::SETUP, "Minute changed to %d", currentMinute);
      }
      // Capture time with millisecond precision (right before display update)
      gettimeofday(&tv, NULL);
      delayAtDrawTime = (float)timeinfo.tm_sec + (float)(tv.tv_usec / 1000) / 1000.0f;
    }
  }
  else
  {
    // Cold boot - also capture time
    struct tm timeinfo;
    struct timeval tv;
    if (getLocalTime(&timeinfo))
    {
      gettimeofday(&tv, NULL);
      delayAtDrawTime = (float)timeinfo.tm_sec + (float)(tv.tv_usec / 1000) / 1000.0f;
    }
  }

  // === Single Display Update ===
  // Now that both WiFi/NTP and sensor data are ready, update display once
  DisplayManager_SetStatus("Running");
  if (DisplayManager_UpdateDisplay(networkState, true))
  {
    LOGI(LogTag::SETUP, "Display updated (single refresh)");
    exportFrameBuffer();
  }
  else
  {
    // No update needed or failed
    EPD_DeepSleep();
    LOGI(LogTag::DISPLAY_MGR, "EPD entered deep sleep");
  }

  // === Adaptive Processing Time Adjustment ===
  // Use delayAtDrawTime captured before display update for accurate measurement (ms precision)
  // Only adjust when WiFi is NOT connected (NTP sync causes variable timing)
  if (!networkState.wifiConnected)
  {
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    float estimated = rtcState.estimatedProcessingTime;

    // Goal: draw as close to minute boundary as possible (delayAtDrawTime = 0)
    // Use smoothing formula: next = current + error * 0.5
    // - If we waited for minute change -> woke too early -> decrease est proportionally
    // - If delayAtDrawTime > 0 -> still late -> increase est proportionally
    if (waitedSeconds > 0.1f) // We had to wait more than 100ms
    {
      // We woke too early - decrease estimated time proportionally
      float adjustment = waitedSeconds * 0.5f;
      float newEstimated = estimated - adjustment;
      LOGI(LogTag::SETUP, "Processing time adjusted: %.2f -> %.2f sec (waited %.2f sec)",
           estimated, newEstimated, waitedSeconds);
      rtcState.estimatedProcessingTime = newEstimated;
    }
    else if (delayAtDrawTime > 0.1f) // More than 100ms late
    {
      // Smoothing: move halfway towards target
      // target = estimated + delayAtDrawTime (what we actually needed)
      // new = estimated + (target - estimated) * 0.5 = estimated + delayAtDrawTime * 0.5
      float adjustment = delayAtDrawTime * 0.5f;
      float newEstimated = estimated + adjustment;
      LOGI(LogTag::SETUP, "Processing time adjusted: %.2f -> %.2f sec (delay: %.2f sec)",
           estimated, newEstimated, delayAtDrawTime);
      rtcState.estimatedProcessingTime = newEstimated;
    }
    else
    {
      LOGD(LogTag::SETUP, "Processing time optimal: %.2f sec (delay: %.3f sec)", estimated, delayAtDrawTime);
    }

    // Clamp to reasonable range: 1 to 20 seconds
    if (rtcState.estimatedProcessingTime < 1.0f)
    {
      rtcState.estimatedProcessingTime = 1.0f;
    }
    else if (rtcState.estimatedProcessingTime > 20.0f)
    {
      rtcState.estimatedProcessingTime = 20.0f;
    }
  }
  else
  {
    LOGD(LogTag::SETUP, "Skipping processing time adjustment (WiFi connected)");
  }

  // Log sensor values to JSONL file
  if (sensorReady && SensorManager_IsInitialized())
  {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      // Get Unix timestamp
      time_t unixTimestamp;
      time(&unixTimestamp);

      // Get RTC drift (only valid if NTP was synced this boot AND drift was successfully calculated)
      int32_t rtcDriftMs = DeepSleepManager_GetLastRtcDriftMs();
      bool driftValid = ntpSyncedThisBoot && DeepSleepManager_IsLastRtcDriftValid();

      float temp = SensorManager_GetTemperature();
      float humidity = SensorManager_GetHumidity();
      uint16_t co2 = SensorManager_GetCO2();
      float batteryVoltage = g_batteryVoltage;
      float batteryPercent = g_batteryPercent;                 // Linear percent for display
      float batteryMax17048Percent = g_batteryMax17048Percent; // MAX17048 for reference
      float batteryChargeRate = g_batteryChargeRate;
      bool batteryCharging = g_batteryCharging;

      if (SensorLogger_LogValues(timeinfo, unixTimestamp, rtcDriftMs, driftValid, temp, humidity, co2, batteryVoltage, batteryPercent, batteryMax17048Percent, batteryChargeRate, batteryCharging))
      {
        LOGI(LogTag::SETUP, "Sensor values logged successfully");
      }
      else
      {
        LOGW(LogTag::SETUP, "Failed to log sensor values");
      }

      // Send batch data to server if WiFi is connected
      // Note: WiFi stays connected because we use delay() instead of light_sleep() when WiFi is active
      if (networkState.wifiConnected)
      {
        RTCState &rtcState = DeepSleepManager_GetRTCState();
        String payload;
        // Reserve memory to prevent reallocations (approx 200 bytes per record * 120 records = 24000 bytes)
        // ESP32 has enough RAM for this
        payload.reserve(24576);

        time_t now;
        time(&now);

        // First boot: get recent 120 readings and set lastUploadedTime to now
        bool isFirstUpload = (rtcState.lastUploadedTime == 0);
        time_t queryTime = rtcState.lastUploadedTime;

        if (isFirstUpload)
        {
          // On first boot, only get last 2 hours of data (approx 120 readings)
          queryTime = now - 7200;
          LOGI(LogTag::SETUP, "First upload - getting recent readings only");
        }

        time_t latestTimestamp = 0;
        // Get up to 120 readings (2 hours worth) to handle upload failures gracefully
        int count = SensorLogger_GetUnsentReadings(queryTime, payload, latestTimestamp, 120);

        if (count > 0)
        {
          LOGI(LogTag::SETUP, "Found %d unsent readings", count);

          // Retry up to 3 times on failure
          bool uploadSuccess = false;
          for (int attempt = 1; attempt <= 3 && !uploadSuccess; attempt++)
          {
            if (attempt > 1)
            {
              LOGI(LogTag::SETUP, "Retry attempt %d/3...", attempt);
              delay(1000); // Wait 1 second before retry
            }

            if (NetworkManager_SendBatchData(payload))
            {
              uploadSuccess = true;
              LOGI(LogTag::SETUP, "Batch data sent successfully");
              // On first upload, set to current time to skip old backlog
              // On normal upload, set to the latest uploaded timestamp
              rtcState.lastUploadedTime = isFirstUpload ? now : latestTimestamp;
              DeepSleepManager_SaveLastUploadedTime(rtcState.lastUploadedTime);
              LOGI(LogTag::SETUP, "Updated last uploaded time to %ld", (long)rtcState.lastUploadedTime);
            }
          }

          if (!uploadSuccess)
          {
            LOGW(LogTag::SETUP, "Failed to send batch data after 3 attempts");
          }
        }
        else
        {
          if (isFirstUpload)
          {
            // No recent data, but still update lastUploadedTime to avoid re-checking old logs
            rtcState.lastUploadedTime = now;
            DeepSleepManager_SaveLastUploadedTime(rtcState.lastUploadedTime);
            LOGI(LogTag::SETUP, "No recent data, initialized last uploaded time to %ld", (long)now);
          }
          else
          {
            LOGI(LogTag::SETUP, "No new data to send (last uploaded: %ld)", (long)rtcState.lastUploadedTime);
          }
        }
      }
      else
      {
        LOGI(LogTag::SETUP, "WiFi not connected, skipping server upload");
      }
    }
    else
    {
      LOGW(LogTag::SETUP, "Cannot log sensor values: time not available");
    }
  }
}

void loop()
{
  // Check if any button is pressed
  // If pressed, do a full screen update and then go back to sleep
  if (checkAnyButton())
  {
    LOGI(LogTag::LOOP, "Button pressed - performing full screen update");
    DisplayManager_FullUpdate(networkState);
    exportFrameBuffer();
    LOGI(LogTag::LOOP, "Full update complete, entering deep sleep...");
  }
  else
  {
    // Display update is already done in setup()
    // After display update, enter deep sleep until next minute
    // This saves significant power compared to running continuously
    LOGI(LogTag::LOOP, "Entering deep sleep...");
  }

  // Note: We keep sensor in idle mode (not power-down) for 1-minute intervals
  // Per Sensirion app note: idle single-shot (~1.5mA) is more efficient than
  // power-cycled single-shot (~2.6mA) for intervals < 380 seconds
  // Also, ASC (Automatic Self-Calibration) only works in idle mode

  // Flush any buffered ERROR/WARN logs to SD card before sleep
  Logger_FlushToSD();

  // Hold I2C pins high during deep sleep to prevent sensor reset
  DeepSleepManager_HoldI2CPins();

  DeepSleepManager_EnterDeepSleep();
  // Code never reaches here - ESP32 will restart after wakeup
}
