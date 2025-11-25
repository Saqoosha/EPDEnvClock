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
#include "sensor_logger.h"

namespace
{
  constexpr size_t kFrameBufferSize = 27200;
  constexpr unsigned long kNtpSyncInterval = 3600000; // 1 hour

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

void handleSensorInitializationResult(bool wakeFromSleep)
{
  DisplayManager_DrawSetupStatus("Initializing Sensor...");
  if (SensorManager_Begin(wakeFromSleep))
  {
    sensorInitialized = true;
    DisplayManager_SetStatus("Sensor OK");
    LOGI(LogTag::SENSOR, "SCD41 sensor is ready!");
    return;
  }

  DisplayManager_SetStatus("Sensor FAILED!");
  LOGW(LogTag::SENSOR, "SCD41 sensor initialization failed!");
  LOGW(LogTag::SENSOR, "Please check connections:");
  LOGW(LogTag::SENSOR, "  SDA -> GPIO %d", SENSOR_I2C_SDA_PIN);
  LOGW(LogTag::SENSOR, "  SCL -> GPIO %d", SENSOR_I2C_SCL_PIN);
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
  DisplayManager_DrawSetupStatus("EPD Ready!");

  // Initialize sensor
  // handleSensorInitializationResult calls SensorManager_Begin internally
  handleSensorInitializationResult(wakeFromSleep);
  sensorInitialized = SensorManager_IsInitialized();

  // Read sensor once after initialization to get initial data for first display
  // We do this here (blocking) to ensure we have data before the display update starts.
  // This prevents blocking DURING the display update, which causes EPD noise.
  if (sensorInitialized)
  {
    LOGD("Sensor", "Reading sensor data (wakeFromSleep=%s)...", wakeFromSleep ? "true" : "false");
    unsigned long readStartTime = millis();

    // Single-shot mode always takes ~5 seconds (measureSingleShot() internally waits 5s)
    // Use 6 second timeout to allow for measurement completion
    unsigned long timeoutMs = 6000;

    if (SensorManager_ReadBlocking(timeoutMs))
    {
      unsigned long readDuration = millis() - readStartTime;
      LOGI("Sensor", "Sensor data read successfully in %lums", readDuration);
    }
    else
    {
      unsigned long readDuration = millis() - readStartTime;
      LOGW("Sensor", "Failed to read sensor data after %lums (timeout was %lums)", readDuration, timeoutMs);
    }
  }

  // Connect WiFi and sync NTP every 60 minutes (1 hour)
  // WiFi is only needed for NTP sync, not for time queries (RTC keeps time)
  if (DeepSleepManager_ShouldSyncWiFiNtp())
  {
    LOGI("Setup", "WiFi/NTP sync needed (1 hour interval)");

    // Save RTC time before NTP sync to calculate drift
    DeepSleepManager_SaveRtcTimeBeforeSync();

    DisplayManager_DrawSetupStatus("Connecting WiFi...");
    if (NetworkManager_ConnectWiFi(networkState, DisplayManager_DrawSetupStatus))
    {
      if (NetworkManager_SyncNtp(networkState, DisplayManager_DrawSetupStatus))
      {
        DeepSleepManager_MarkNtpSynced(); // Mark NTP as synced and calculate drift
        Logger_SetNtpSynced(true);        // Update logger with NTP sync status
        ntpSyncedThisBoot = true;         // Mark that NTP synced THIS boot
        LOGI("Setup", "WiFi/NTP sync completed");
      }
      else
      {
        networkState.ntpSynced = false;
        Logger_SetNtpSynced(false);
        LOGW("Setup", "NTP sync failed");
        // Setup timezone and restore time from RTC when NTP sync fails
        if (NetworkManager_SetupTimeFromRTC())
        {
          LOGI("Setup", "Time restored from RTC after NTP failure");
        }
        else
        {
          LOGW("Setup", "Failed to restore time from RTC");
        }
      }
    }
    else
    {
      networkState.wifiConnected = false;
      LOGW("Setup", "WiFi connection failed");
      // Setup timezone and restore time from RTC when WiFi/NTP fails
      if (NetworkManager_SetupTimeFromRTC())
      {
        LOGI("Setup", "Time restored from RTC after WiFi failure");
      }
      else
      {
        LOGW("Setup", "Failed to restore time from RTC");
      }
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

  // Initialize sensor logger (after SD card is initialized by DeepSleepManager_Init)
  SensorLogger_Init();

  // Show "Starting..." before final display update
  DisplayManager_DrawSetupStatus("Starting...");
  DisplayManager_SetStatus("Running");
  updateDisplay(true);

  // Log sensor values to JSONL file
  if (sensorInitialized && SensorManager_IsInitialized())
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
      int batteryRawADC = DisplayManager_GetBatteryRawADC();
      float batteryVoltage = g_batteryVoltage;

      if (SensorLogger_LogValues(timeinfo, unixTimestamp, rtcDriftMs, driftValid, temp, humidity, co2, batteryRawADC, batteryVoltage))
      {
        LOGI(LogTag::SETUP, "Sensor values logged successfully");
      }
      else
      {
        LOGW(LogTag::SETUP, "Failed to log sensor values");
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

  // Hold I2C pins high during deep sleep to prevent sensor reset
  DeepSleepManager_HoldI2CPins();

  DeepSleepManager_EnterDeepSleep();
  // Code never reaches here - ESP32 will restart after wakeup
}
