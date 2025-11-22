#include <time.h>
#include <WiFi.h>

#include "display_manager.h"
#include "imagebw_export.h"
#include "network_manager.h"
#include "sensor_manager.h"
#include "server_config.h"
#include "EPD_Init.h"
#include "deep_sleep_manager.h"

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

void handleSensorInitializationResult()
{
  DisplayManager_DrawSetupStatus("Initializing Sensor...");
  if (SensorManager_Begin())
  {
    sensorInitialized = true;
    DisplayManager_SetStatus("Sensor OK");
    Serial.println("SDC41 sensor initialized successfully!");
    return;
  }

  DisplayManager_SetStatus("Sensor Failed!");
  Serial.println("Warning: SDC41 sensor initialization failed!");
  Serial.println("Please check connections:");
  Serial.println("  SDA -> GPIO 38");
  Serial.println("  SCL -> GPIO 21");
  Serial.println("  VDD -> 3.3V");
  Serial.println("  GND -> GND");
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
  delay(1000);
  Serial.println("\n\n=== EPD Clock with SDC41 Sensor ===");

  // Initialize deep sleep manager first (checks if wake from sleep)
  DeepSleepManager_Init();
  const bool wakeFromSleep = DeepSleepManager_IsWakeFromSleep();

  if (wakeFromSleep)
  {
    Serial.println("[Setup] Woke from deep sleep");
  }
  else
  {
    Serial.println("[Setup] Cold boot");
  }

  randomSeed(analogRead(0));

  // Initialize display manager with wakeFromSleep flag
  // This determines if we do full init or minimal wake-up
  DisplayManager_Init(wakeFromSleep);
  DisplayManager_DrawSetupStatus("EPD Ready!");

  // Initialize sensor
  // handleSensorInitializationResult calls SensorManager_Begin internally
  handleSensorInitializationResult();
  sensorInitialized = SensorManager_IsInitialized();

  // Read sensor once after initialization to get initial data for first display
  // Use blocking read to ensure we have data before first display
  if (sensorInitialized)
  {
    Serial.println("Reading initial sensor data...");
    if (SensorManager_ReadBlocking(10000))
    {
      Serial.println("Initial sensor data read successfully");
    }
    else
    {
      Serial.println("Warning: Failed to read initial sensor data");
    }
  }

  // Connect WiFi and sync NTP only once per hour
  // WiFi is only needed for NTP sync, not for time queries (RTC keeps time)
  if (DeepSleepManager_ShouldSyncWiFiNtp())
  {
    Serial.println("[Setup] WiFi/NTP sync needed (1 hour interval)");
    DisplayManager_DrawSetupStatus("Connecting WiFi...");
    if (NetworkManager_ConnectWiFi(networkState, DisplayManager_DrawSetupStatus))
    {
      if (NetworkManager_SyncNtp(networkState, DisplayManager_DrawSetupStatus))
      {
        networkState.lastNtpSync = millis();
        DeepSleepManager_MarkNtpSynced(); // Mark NTP as synced in RTC memory
        Serial.println("[Setup] WiFi/NTP sync completed");
      }
      else
      {
        networkState.ntpSynced = false;
        Serial.println("[Setup] NTP sync failed");
      }
    }
    else
    {
      networkState.ntpSynced = false;
      Serial.println("[Setup] WiFi connection failed");
    }
  }
  else
  {
    // WiFi/NTP sync not needed (less than 1 hour since last sync)
    // Skip WiFi connection - RTC keeps time from previous NTP sync
    Serial.println("[Setup] Skipping WiFi/NTP sync (less than 1 hour since last sync)");
    RTCState &rtcState = DeepSleepManager_GetRTCState();
    Serial.print("[Setup] Last sync was ");
    Serial.print(rtcState.bootCount - rtcState.lastNtpSyncBootCount);
    Serial.println(" boots ago");

    // No WiFi connection needed - RTC maintains time from previous sync
    networkState.wifiConnected = false;
    networkState.ntpSynced = true; // Assume still synced (RTC keeps time)
    Serial.println("[Setup] Using RTC time (no WiFi connection)");
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
  Serial.println("[Loop] Entering deep sleep...");
  DeepSleepManager_EnterDeepSleep();
  // Code never reaches here - ESP32 will restart after wakeup
}
