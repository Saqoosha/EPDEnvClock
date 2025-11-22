#include <time.h>
#include <WiFi.h>

#include "display_manager.h"
#include "imagebw_export.h"
#include "network_manager.h"
#include "sensor_manager.h"
#include "server_config.h"
#include "EPD_Init.h"
#include "deep_sleep_manager.h"
#include "logger.h"

namespace
{
  constexpr size_t kFrameBufferSize = 27200;
  constexpr unsigned long kNtpSyncInterval = 3600000; // 1 hour

  NetworkState networkState;
  bool sensorInitialized = false;

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

void handleSensorInitializationResult(bool wakeFromSleep)
{
  DisplayManager_DrawSetupStatus("Initializing Sensor...");
  if (SensorManager_Begin(wakeFromSleep))
  {
    sensorInitialized = true;
    DisplayManager_SetStatus("Sensor OK");
    LOGI(LogTag::SENSOR, "SDC41 sensor is ready!");
    return;
  }

  DisplayManager_SetStatus("Sensor FAILED!");
  LOGW(LogTag::SENSOR, "SDC41 sensor initialization failed!");
  LOGW(LogTag::SENSOR, "Please check connections:");
  LOGW(LogTag::SENSOR, "  SDA -> GPIO 38");
  LOGW(LogTag::SENSOR, "  SCL -> GPIO 21");
  LOGW(LogTag::SENSOR, "  VDD -> 3.3V");
  LOGW(LogTag::SENSOR, "  GND -> GND");
}

void updateDisplay(bool forceUpdate)
{
  if (DisplayManager_UpdateDisplay(networkState, forceUpdate))
  {
    exportFrameBuffer();
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);

  // Release I2C pins hold if they were held during deep sleep
  DeepSleepManager_ReleaseI2CPins();

  delay(1000);

  // Initialize logger (default: DEBUG level, BOTH timestamp mode)
  Logger_Init(LogLevel::DEBUG, TimestampMode::BOTH);

  LOGI(LogTag::SETUP, "=== EPD Clock with SDC41 Sensor ===");

  // Initialize deep sleep manager first (checks if wake from sleep)
  DeepSleepManager_Init();
  const bool wakeFromSleep = DeepSleepManager_IsWakeFromSleep();

  if (wakeFromSleep)
  {
    LOGI(LogTag::SETUP, "Woke from deep sleep");
  }
  else
  {
    LOGI(LogTag::SETUP, "Cold boot");
  }

  randomSeed(analogRead(0));

  // Initialize display manager with wakeFromSleep flag
  // This determines if we do full init or minimal wake-up
  DisplayManager_Init(wakeFromSleep);
  DisplayManager_DrawSetupStatus("EPD Ready!");

  // Initialize sensor
  // handleSensorInitializationResult calls SensorManager_Begin internally
  handleSensorInitializationResult(wakeFromSleep);
  sensorInitialized = SensorManager_IsInitialized();

  // Read sensor once after initialization to get initial data for first display
  // Use blocking read to ensure we have data before first display
  if (sensorInitialized)
  {
    LOGD("Sensor", "Reading sensor data (wakeFromSleep=%s)...", wakeFromSleep ? "true" : "false");
    unsigned long readStartTime = millis();

    // Optimize timeout based on wake state:
    // - wakeFromSleep=true: Sensor is already running, data should be ready immediately (timeout: 2s)
    // - wakeFromSleep=false: After initialization + 5s delay, data should be ready (timeout: 5s)
    unsigned long timeoutMs = wakeFromSleep ? 2000 : 5000;

    if (SensorManager_ReadBlocking(timeoutMs))
    {
      unsigned long readDuration = millis() - readStartTime;
      LOGI("Sensor", "Sensor data read successfully in %lums", readDuration);
    }
    else
    {
      unsigned long readDuration = millis() - readStartTime;
      LOGW("Sensor", "Failed to read sensor data after %lums (timeout was %lums)", readDuration, timeoutMs);

      // If blocking read failed, try non-blocking read as fallback
      LOGD("Sensor", "Trying non-blocking read as fallback...");
      SensorManager_Read();
    }
  }

  // Connect WiFi and sync NTP only once per hour
  // WiFi is only needed for NTP sync, not for time queries (RTC keeps time)
  if (DeepSleepManager_ShouldSyncWiFiNtp())
  {
    LOGI("Setup", "WiFi/NTP sync needed (1 hour interval)");
    DisplayManager_DrawSetupStatus("Connecting WiFi...");
    if (NetworkManager_ConnectWiFi(networkState, DisplayManager_DrawSetupStatus))
    {
      if (NetworkManager_SyncNtp(networkState, DisplayManager_DrawSetupStatus))
      {
        DeepSleepManager_MarkNtpSynced(); // Mark NTP as synced in RTC memory
        Logger_SetNtpSynced(true);        // Update logger with NTP sync status
        LOGI("Setup", "WiFi/NTP sync completed");
      }
      else
      {
        networkState.ntpSynced = false;
        Logger_SetNtpSynced(false);
        LOGW("Setup", "NTP sync failed");
      }
    }
    else
    {
      networkState.wifiConnected = false;
      LOGW("Setup", "WiFi connection failed");
    }
  }
  else
  {
    // WiFi/NTP sync not needed (less than 1 hour since last sync)
    // Skip WiFi connection - RTC keeps time from previous NTP sync
    LOGI("Setup", "Skipping WiFi/NTP sync (less than 1 hour since last sync)");
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    LOGI("Setup", "Last sync was %d boots ago", rtcState.bootCount - rtcState.lastNtpSyncBootCount);

    // No WiFi connection needed - RTC maintains time from previous sync
    networkState.wifiConnected = false;
    networkState.ntpSynced = true; // Assume still synced (RTC keeps time)
    Logger_SetNtpSynced(true);     // Assume NTP is still synced (RTC keeps time)
    LOGI("Setup", "Using RTC time (no WiFi connection)");
  }

  DisplayManager_DrawSetupStatus("Starting...");
  DisplayManager_SetStatus("Running");
  updateDisplay(true);
}

void loop()
{
  // Display update is already done in setup()
  // After display update, enter deep sleep until next minute
  // This saves significant power compared to running continuously
  LOGI(LogTag::LOOP, "Entering deep sleep...");

  // Hold I2C pins high during deep sleep to prevent sensor reset
  DeepSleepManager_HoldI2CPins();

  DeepSleepManager_EnterDeepSleep();
  // Code never reaches here - ESP32 will restart after wakeup
}
