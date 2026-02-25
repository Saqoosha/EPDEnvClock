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

  // Determine if full NTP sync is needed (top of hour) vs drift measurement only
  bool needFullNtpSync = DeepSleepManager_ShouldSyncWiFiNtp();

  // TEMPORARY DIAGNOSTIC FEATURE (Dec 2025):
  // Measure NTP drift every boot for RTC drift analysis.
  // This increases battery consumption by ~50% due to WiFi every boot.
  // To enable: set measureDriftOnly = !needFullNtpSync
  // See docs/RTC_DEEP_SLEEP.md for details.
  bool measureDriftOnly = false; // Disabled - only sync hourly to save battery

  bool skipWifiDueToLowBattery = false;

  // Check if battery allows WiFi connection
  // Skip WiFi only if battery voltage is genuinely low (not sensor error)
  // Sensor error (-1.0V) should NOT block WiFi - we can't know battery state
  // Exception: allow WiFi when charging even if voltage is low
  if (g_batteryVoltage >= 0.0f && g_batteryVoltage < kWifiMinBatteryVoltage && !g_batteryCharging)
  {
    LOGW("Setup", "Skipping WiFi: low battery (%.3fV)", g_batteryVoltage);
    skipWifiDueToLowBattery = true;
    needFullNtpSync = false;
    measureDriftOnly = false;

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
  else
  {
    if (g_batteryVoltage < 0.0f)
    {
      LOGW("Setup", "Battery sensor error (%.3fV) - WiFi NOT blocked (battery state unknown)", g_batteryVoltage);
    }

    if (needFullNtpSync)
    {
      LOGI("Setup", "WiFi/NTP sync needed (top of hour or 30min)");
    }
    else if (measureDriftOnly)
    {
      LOGI("Setup", "WiFi for drift measurement only");
      RTCState &rtcState = DeepSleepManager_GetRTCState();
      LOGI("Setup", "Last sync was %d boots ago", rtcState.bootCount - rtcState.lastNtpSyncBootCount);
    }
    else
    {
      LOGI("Setup", "No WiFi needed, using RTC time");
    }
  }

  // Show status before starting tasks
  bool needWifi = needFullNtpSync || measureDriftOnly;
  if (needWifi)
  {
    DisplayManager_DrawSetupStatus("WiFi + Sensor...");
  }
  else
  {
    DisplayManager_DrawSetupStatus("Reading Sensor...");
  }

  unsigned long taskStartTime = millis();
  bool sensorReady = false;
  int32_t measuredDriftMs = 0;
  bool driftMeasured = false;
  int64_t measuredCumulativeCompMs = 0;  // Cumulative compensation before NTP reset

  if (needWifi)
  {
    // === WiFi needed: Use parallel processing (dual-core) ===
    // Run WiFi connection and sensor reading in parallel
    // WiFi is used for either full NTP sync (top of hour) or drift measurement (other times)
    LOGI("Setup", "Starting parallel tasks (WiFi + Sensor)");
    ParallelTasks_StartWiFiAndSensor(wakeFromSleep, needFullNtpSync, measureDriftOnly);

    // Wait for both tasks to complete (20 second timeout)
    bool parallelSuccess = ParallelTasks_WaitForCompletion(20000);

    // Get results from parallel tasks
    ParallelTaskResults &results = ParallelTasks_GetResults();
    networkState = ParallelTasks_GetNetworkState();
    sensorInitialized = results.sensorInitialized;
    sensorReady = results.sensorReady;
    driftMeasured = results.driftMeasured;
    measuredDriftMs = results.ntpDriftMs;
    measuredCumulativeCompMs = results.cumulativeCompMs;

    unsigned long taskDuration = millis() - taskStartTime;
    LOGI("Setup", "Parallel tasks completed in %lu ms (WiFi:%d, NTP:%d, Drift:%d ms, Sensor:%d)",
         taskDuration,
         results.wifiConnected ? 1 : 0,
         results.ntpSynced ? 1 : 0,
         driftMeasured ? measuredDriftMs : 0,
         results.sensorReady ? 1 : 0);

    // Track if NTP synced this boot for drift calculation
    if (results.ntpSynced)
    {
      ntpSyncedThisBoot = true;
    }
  }
  else
  {
    // === No WiFi (low battery): Use single-core with light_sleep ===
    // This saves power by using light_sleep during sensor measurement wait
    // (~0.8mA vs ~20mA with delay())
    LOGI("Setup", "Starting sensor-only task (with light_sleep, no WiFi)");

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
    networkState.ntpSynced = false;
    Logger_SetNtpSynced(true); // RTC time is still valid

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
  // Signed seconds relative to the minute boundary:
  // - negative: we're still BEFORE the boundary (early)
  // - positive: we're AFTER the boundary (late)
  float drawOffsetSec = 0.0f;
  float waitedSeconds = 0.0f;   // How long we waited for minute to change
  // EPD partial update takes ~0.65-0.75s (EPD_Display + PartUpdate).
  // Start updating this much BEFORE the minute boundary so the visible flip lands on the boundary.
  constexpr float kEpdLeadTimeSec = 0.70f;
  constexpr int32_t kEpdLeadTimeMs = (int32_t)(kEpdLeadTimeSec * 1000.0f);

  // When we start updating before the boundary, we must render the *next minute*.
  struct tm overrideTimeinfo;
  const struct tm *overrideTimePtr = nullptr;
  auto computeOffsetSec = [&](float &outOffsetSec) -> bool {
    struct timeval tvNow;
    gettimeofday(&tvNow, NULL);
    time_t nowSec = (time_t)tvNow.tv_sec;
    struct tm tmNow;
    if (localtime_r(&nowSec, &tmNow) == nullptr)
    {
      return false;
    }
    // Map ms into signed range around the boundary (e.g. 59.900s -> -0.100s)
    int32_t phaseMs = (int32_t)tmNow.tm_sec * 1000 + (int32_t)(tvNow.tv_usec / 1000);
    if (phaseMs > 30000)
    {
      phaseMs -= 60000;
    }
    outOffsetSec = (float)phaseMs / 1000.0f;
    return true;
  };
  if (wakeFromSleep)
  {
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t nowSec = (time_t)tv.tv_sec;
    struct tm timeinfo;
    if (localtime_r(&nowSec, &timeinfo) != nullptr)
    {
      uint8_t currentMinute = timeinfo.tm_min;
      if (currentMinute == rtcState.lastDisplayedMinute)
      {
        // Calculate exact milliseconds until next minute boundary
        const int32_t msIntoMinute = (int32_t)timeinfo.tm_sec * 1000 + (int32_t)(tv.tv_usec / 1000);
        int32_t msUntilNextMinute = 60000 - msIntoMinute;
        if (msUntilNextMinute < 0)
        {
          msUntilNextMinute = 0;
        }

        // Wait only until we're within the EPD lead-time window.
        int32_t waitMs = msUntilNextMinute - kEpdLeadTimeMs;
        if (waitMs > 0)
        {
          waitedSeconds = (float)waitMs / 1000.0f; // Record wait time for adjustment
          LOGI(LogTag::SETUP, "Still same minute (%d), waiting %d ms (lead %d ms) for boundary...",
               currentMinute, waitMs, kEpdLeadTimeMs);
          delay((unsigned long)waitMs);
        }

        // Recompute remaining ms to boundary after waiting, and pre-render the next minute.
        gettimeofday(&tv, NULL);
        nowSec = (time_t)tv.tv_sec;
        if (localtime_r(&nowSec, &timeinfo) != nullptr)
        {
          currentMinute = timeinfo.tm_min;
          const int32_t msIntoMinute2 = (int32_t)timeinfo.tm_sec * 1000 + (int32_t)(tv.tv_usec / 1000);
          msUntilNextMinute = 60000 - msIntoMinute2;
          if (msUntilNextMinute < 0) msUntilNextMinute = 0;

          time_t displayEpoch = nowSec + (time_t)((msUntilNextMinute + 999) / 1000); // ceil to cross boundary
          if (localtime_r(&displayEpoch, &overrideTimeinfo) != nullptr)
          {
            overrideTimePtr = &overrideTimeinfo;
            LOGD(LogTag::SETUP, "Pre-boundary render: now=%02d:%02d:%02d, render=%02d:%02d (msToBoundary=%d)",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                 overrideTimeinfo.tm_hour, overrideTimeinfo.tm_min, msUntilNextMinute);
          }
        }
      }
      // Capture time offset with millisecond precision (right before display update)
      (void)computeOffsetSec(drawOffsetSec);
    }
  }
  else
  {
    // Cold boot - also capture time
    (void)computeOffsetSec(drawOffsetSec);
  }

  // === Single Display Update ===
  // Now that both WiFi/NTP and sensor data are ready, update display once
  // Capture timing around the EPD refresh so we can compare with human-observed flip delay.
  float displayStartOffsetSec = 0.0f;
  float displayEndOffsetSec = 0.0f;
  struct timeval tvDisplayStart;
  struct timeval tvDisplayEnd;
  gettimeofday(&tvDisplayStart, NULL);
  (void)computeOffsetSec(displayStartOffsetSec);

  DisplayManager_SetStatus("Running");
  const bool displayUpdated = DisplayManager_UpdateDisplay(networkState, true, overrideTimePtr);

  gettimeofday(&tvDisplayEnd, NULL);
  (void)computeOffsetSec(displayEndOffsetSec);
  const int64_t displayWallMs =
      (int64_t)(tvDisplayEnd.tv_sec - tvDisplayStart.tv_sec) * 1000LL +
      (int64_t)(tvDisplayEnd.tv_usec - tvDisplayStart.tv_usec) / 1000LL;
  LOGI(LogTag::SETUP, "EPD refresh timing: startOffset=%.3fs endOffset=%.3fs wall=%lldms (waited=%.3fs)",
       displayStartOffsetSec, displayEndOffsetSec, (long long)displayWallMs, waitedSeconds);

  if (displayUpdated)
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
  // Use drawOffsetSec captured before display update for accurate measurement (ms precision)
  // Update the appropriate estimate (WiFi/no-WiFi) so top-of-hour updates can also converge.
  // Skip cold boot: its one-time init cost would distort the steady-state estimate.
  if (wakeFromSleep)
  {
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    const bool usedWifiThisBoot = networkState.wifiConnected;
    float &estimateRef = usedWifiThisBoot ? rtcState.estimatedProcessingTimeWifi : rtcState.estimatedProcessingTimeNoWifi;
    float estimated = estimateRef;
    const float targetOffsetSec = -kEpdLeadTimeSec;

    // Calculate actual delay considering measured NTP drift
    // Drift = NTP time - System time
    // Positive drift = system is behind (slow), so actual delay is larger
    float actualOffsetAtDrawTime = drawOffsetSec;
    // Only apply drift correction when we measured drift WITHOUT setting the system clock.
    // For full NTP sync boots, the system clock has already been corrected.
    if (driftMeasured && !ntpSyncedThisBoot)
    {
      actualOffsetAtDrawTime += (float)measuredDriftMs / 1000.0f;
      LOGD(LogTag::SETUP, "Draw offset corrected for drift: %.3f + %.3f = %.3f sec",
           drawOffsetSec, (float)measuredDriftMs / 1000.0f, actualOffsetAtDrawTime);
    }

    // Goal: start drawing around -kEpdLeadTimeSec so the *visible* refresh lands on the boundary.
    // Use smoothing formula: next = current + error * 0.5
    // - If we waited for minute change -> woke too early -> decrease est proportionally
    // - If offset != 0 -> adjust in both directions (early/late)
    if (waitedSeconds > 0.1f) // We had to wait more than 100ms
    {
      // We woke too early - decrease estimated time proportionally
      float adjustment = waitedSeconds * 0.5f;
      float newEstimated = estimated - adjustment;
      LOGI(LogTag::SETUP, "Processing time adjusted (%s): %.2f -> %.2f sec (waited %.3f sec)",
           usedWifiThisBoot ? "wifi" : "no-wifi", estimated, newEstimated, waitedSeconds);
      estimateRef = newEstimated;
    }
    else if (fabsf(actualOffsetAtDrawTime - targetOffsetSec) > 0.1f) // More than 100ms off target
    {
      // Smoothing: move halfway towards target
      // error = actual - target
      const float error = actualOffsetAtDrawTime - targetOffsetSec;
      float adjustment = error * 0.5f;
      float newEstimated = estimated + adjustment;
      LOGI(LogTag::SETUP, "Processing time adjusted (%s): %.2f -> %.2f sec (draw offset: %.3f sec, target: %.3f sec)",
           usedWifiThisBoot ? "wifi" : "no-wifi", estimated, newEstimated, actualOffsetAtDrawTime, targetOffsetSec);
      estimateRef = newEstimated;
    }
    else
    {
      LOGD(LogTag::SETUP, "Processing time optimal (%s): %.2f sec (draw offset: %.3f sec)",
           usedWifiThisBoot ? "wifi" : "no-wifi", estimated, actualOffsetAtDrawTime);
    }

    // Clamp to reasonable range: 1 to 20 seconds
    if (estimateRef < 1.0f)
    {
      estimateRef = 1.0f;
    }
    else if (estimateRef > 20.0f)
    {
      estimateRef = 20.0f;
    }

    // Persist if changed meaningfully (>=50ms) to survive power cycles
    float change = estimateRef - estimated;
    if (change < 0)
    {
      change = -change;
    }
    if (change > 0.05f)
    {
      DeepSleepManager_SaveEstimatedProcessingTimes(rtcState.estimatedProcessingTimeNoWifi, rtcState.estimatedProcessingTimeWifi);
    }
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

      // Get RTC drift - now measured every boot (not just at hourly sync)
      // measuredDriftMs and driftMeasured were set earlier by parallel tasks
      // For hourly sync: drift is from DeepSleepManager (system clock was corrected)
      // For drift-only measurement: drift is from NTPClient (system clock unchanged)
      int32_t rtcDriftMs = measuredDriftMs;
      bool driftValid = driftMeasured;

      // Get cumulative compensation (saved before MarkNtpSynced reset) and current drift rate
      RTCState &logRtcState = DeepSleepManager_GetRTCState();
      int64_t cumulativeCompMs = measuredCumulativeCompMs;
      float driftRateMsPerMin = logRtcState.driftRateMsPerMin;

      float temp = SensorManager_GetTemperature();
      float humidity = SensorManager_GetHumidity();
      uint16_t co2 = SensorManager_GetCO2();
      float batteryVoltage = g_batteryVoltage;
      float batteryPercent = g_batteryPercent;                 // Linear percent for display
      float batteryMax17048Percent = g_batteryMax17048Percent; // MAX17048 for reference
      float batteryChargeRate = g_batteryChargeRate;
      bool batteryCharging = g_batteryCharging;

      if (SensorLogger_LogValues(timeinfo, unixTimestamp, rtcDriftMs, cumulativeCompMs, driftRateMsPerMin, driftValid, temp, humidity, co2, batteryVoltage, batteryPercent, batteryMax17048Percent, batteryChargeRate, batteryCharging))
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
