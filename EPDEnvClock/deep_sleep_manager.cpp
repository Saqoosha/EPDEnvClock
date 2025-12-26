#include "deep_sleep_manager.h"
#include "spi.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp32/clk.h>
#include <time.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <SD.h>
#include <sys/time.h>
#include "logger.h"
#include "sensor_manager.h"

namespace
{
// RTC memory attribute ensures this persists across deep sleep
RTC_DATA_ATTR RTCState rtcState;

// SD card pin configuration (SPI interface)
// Using HSPI bus (separate from EPD display which uses bit-banging SPI)
// Based on official 5.79" E-Paper example from CrowPanel
// EPD uses bit-banging SPI on pins 11, 12, 45, 46, 47, 48
// SD card uses hardware HSPI on different pins
constexpr int SD_MOSI_PIN = 40;  // SD card MOSI
constexpr int SD_MISO_PIN = 13;  // SD card MISO
constexpr int SD_SCK_PIN = 39;   // SD card SCK
constexpr int SD_CS_PIN = 10;    // SD card CS
constexpr int SD_POWER_PIN = 42; // SD card power enable

// Create an instance of SPIClass for SD card SPI communication (HSPI bus)
SPIClass SD_SPI = SPIClass(HSPI);

// We use SD card for image storage (or SPIFFS as fallback)
// SD card has much better write endurance than SPIFFS Flash memory
constexpr char kFrameBufferFile[] = "/frame.bin";
constexpr char kLastUploadedTimeFile[] = "/last_uploaded.txt";
constexpr char kDriftRateFile[] = "/drift_rate.txt";

bool sdCardAvailable = false;
bool spiffsMounted = false;
bool initialized = false;
struct timeval rtcTimeBeforeNtpSync = {0, 0}; // Stores RTC time before NTP sync attempt (with microseconds)
unsigned long ntpSyncDurationMs = 0;          // Duration of NTP sync wait time (ms) - RTC continues running during this time

void restoreTimeFromRTC()
{
  if (rtcState.savedTime > 0)
  {
    // Use ESP32's RTC timer to measure actual elapsed time
    // esp_timer_get_time() returns microseconds since boot
    // We store the boot time in RTC memory to calculate elapsed time accurately
    static bool bootTimeStored = false;
    static int64_t bootTimeUs = 0;

    if (!bootTimeStored)
    {
      // Store boot time on first call (this function is called early in init)
      bootTimeUs = esp_timer_get_time();
      bootTimeStored = true;
    }

    // Calculate elapsed time since sleep started
    // sleepDurationUs is the intended sleep duration
    // Actual elapsed time = intended sleep duration (deep sleep is precise)
    // Add small offset for boot time (measured from boot to this point)
    int64_t currentTimeUs = esp_timer_get_time();
    int64_t bootOverheadUs = currentTimeUs; // Time since boot until now

    // Calculate wakeup time with microsecond precision to avoid truncation drift
    // Previous bug: integer division caused up to 1 second loss per cycle,
    // accumulating to ~1 minute per hour of drift
    int64_t savedTimeUs = (int64_t)rtcState.savedTime * 1000000LL + rtcState.savedTimeUs;
    int64_t wakeupTimeUs = savedTimeUs + (int64_t)rtcState.sleepDurationUs + bootOverheadUs;

    // Apply RTC drift compensation
    // RTC slow clock runs slower than nominal, causing time to fall behind
    // Compensate by adding the expected drift based on sleep duration
    float sleepMinutes = (float)rtcState.sleepDurationUs / 60000000.0f;
    int64_t driftCompensationUs = (int64_t)(sleepMinutes * rtcState.driftRateMsPerMin * 1000.0f);
    wakeupTimeUs += driftCompensationUs;

    // Track cumulative compensation for accurate drift rate calculation
    // This is reset when NTP sync occurs
    rtcState.cumulativeCompensationMs += driftCompensationUs / 1000;

    struct timeval tv;
    tv.tv_sec = (time_t)(wakeupTimeUs / 1000000LL);
    tv.tv_usec = (suseconds_t)(wakeupTimeUs % 1000000LL);
    settimeofday(&tv, NULL);

    // Re-apply timezone
    setenv("TZ", "JST-9", 1);
    tzset();

    // ctime() includes newline, so we format manually
    struct tm *timeinfo = localtime(&tv.tv_sec);
    LOGI(LogTag::DEEPSLEEP, "Time restored: %04d-%02d-%02d %02d:%02d:%02d.%03ld",
         timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
         tv.tv_usec / 1000);
    LOGD(LogTag::DEEPSLEEP, "Drift compensation: +%.0f ms (rate: %.1f ms/min, sleep: %.2f min, cumulative: %lld ms)",
         driftCompensationUs / 1000.0f, rtcState.driftRateMsPerMin, sleepMinutes, rtcState.cumulativeCompensationMs);
  }
}

} // namespace

void DeepSleepManager_Init()
{
  if (initialized)
  {
    return;
  }

  // Increment boot count on each wake
  rtcState.bootCount++;

  // Log RTC slow clock calibration value
  // The calibration value is the period of one RTC_SLOW_CLK cycle in Q13.19 fixed-point format
  // ESP32 uses this to correct timer values, reducing raw oscillator error (~5%) to residual drift (~0.3%)
  uint32_t cal = esp_clk_slowclk_cal_get();
  float period_us = (float)cal / (1 << 19);  // Convert Q13.19 to float
  float freq_khz = 1000.0f / period_us;  // Actual frequency in kHz
  LOGD(LogTag::DEEPSLEEP, "RTC slow clock: %.2f kHz (period: %.3f us, cal: %u)", freq_khz, period_us, cal);

  // Validate RTC state
  if (rtcState.magic != 0xDEADBEEF)
  {
    // First boot or invalid RTC data - reset state
    rtcState.magic = 0xDEADBEEF;
    rtcState.lastDisplayedMinute = 255;
    rtcState.sensorInitialized = false;
    rtcState.bootCount = 1;
    rtcState.lastNtpSyncBootCount = 0;
    rtcState.lastNtpSyncTime = 0;
    rtcState.lastRtcDriftMs = 0;
    rtcState.lastRtcDriftValid = false;
    rtcState.imageSize = 0;
    rtcState.savedTime = 0;
    rtcState.savedTimeUs = 0;
    rtcState.sleepDurationUs = 0;
    rtcState.estimatedProcessingTime = 5.0f; // Default 5 seconds
  }
  else
  {
    // Restore time if not syncing via NTP immediately
    // But we only know if we sync NTP later.
    // It's safe to restore it now; NTP will overwrite it if needed.
    restoreTimeFromRTC();
  }

  // Try to initialize SD card first (preferred for write endurance)
  // Enable SD card power (GPIO 42 must be HIGH for SD card to work)
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, HIGH);
  delay(10); // Brief delay to ensure power stability

  // Initialize HSPI bus for SD card with specified pins
  SD_SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

  // Try to mount SD card with 80MHz SPI clock speed (same as official example)
  if (SD.begin(SD_CS_PIN, SD_SPI, 80000000))
  {
    sdCardAvailable = true;
    LOGI(LogTag::DEEPSLEEP, "SD card initialized successfully, size: %llu MB", SD.cardSize() / (1024 * 1024));
  }
  else
  {
    sdCardAvailable = false;
    LOGW(LogTag::DEEPSLEEP, "SD card initialization failed, falling back to SPIFFS");
    LOGW(LogTag::DEEPSLEEP, "WARNING: Using SPIFFS fallback - Flash memory write endurance is limited!");
    LOGW(LogTag::DEEPSLEEP, "WARNING: SPIFFS has 10,000-100,000 write cycles. Consider using SD card for better durability.");

    // Fallback to SPIFFS if SD card is not available
    if (!SPIFFS.begin(true))
    {
      spiffsMounted = false;
      LOGE(LogTag::DEEPSLEEP, "SPIFFS Mount Failed");
      LOGE(LogTag::DEEPSLEEP, "ERROR: No storage available! Frame buffer will not be saved.");
      // Continue initialization but frame buffer save/load will fail gracefully
    }
    else
    {
      spiffsMounted = true;
      LOGI(LogTag::DEEPSLEEP, "SPIFFS Mounted (fallback)");
      LOGI(LogTag::DEEPSLEEP, "SPIFFS Storage: %u / %u bytes used", SPIFFS.usedBytes(), SPIFFS.totalBytes());
    }
  }

  initialized = true;

  // Restore lastUploadedTime from SD card if not in RTC memory
  // This ensures we don't lose upload history across power cycles
  if (rtcState.lastUploadedTime == 0)
  {
    time_t storedTime = DeepSleepManager_LoadLastUploadedTime();
    if (storedTime > 0)
    {
      rtcState.lastUploadedTime = storedTime;
      LOGI(LogTag::DEEPSLEEP, "Restored lastUploadedTime from storage: %ld", (long)storedTime);
    }
  }

  // Restore driftRateMsPerMin from SD card if not calibrated in RTC memory
  // This allows drift rate to persist across power cycles/reboots
  if (!rtcState.driftRateCalibrated)
  {
    float storedRate = DeepSleepManager_LoadDriftRate();
    if (storedRate > 0)
    {
      rtcState.driftRateMsPerMin = storedRate;
      rtcState.driftRateCalibrated = true;
      LOGI(LogTag::DEEPSLEEP, "Restored driftRate from storage: %.1f ms/min", storedRate);
    }
  }

  LOGI(LogTag::DEEPSLEEP, "Boot count: %u", rtcState.bootCount);
  LOGI(LogTag::DEEPSLEEP, "Last displayed minute: %u", rtcState.lastDisplayedMinute);
}

bool DeepSleepManager_IsWakeFromSleep()
{
  // If boot count > 1, we've woken from sleep at least once
  return rtcState.bootCount > 1;
}

RTCState &DeepSleepManager_GetRTCState()
{
  return rtcState;
}

uint64_t DeepSleepManager_CalculateSleepDuration()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // If we can't get time, sleep for 60 seconds
    LOGW(LogTag::DEEPSLEEP, "Warning: Cannot get local time, sleeping for 60 seconds");
    return 60 * 1000000ULL; // 60 seconds in microseconds
  }

  const uint8_t currentMinute = timeinfo.tm_min;
  const uint8_t currentSecond = timeinfo.tm_sec;

  // Get current time with millisecond precision
  struct timeval tv;
  gettimeofday(&tv, NULL);
  float currentMs = (float)(tv.tv_usec / 1000); // Milliseconds part

  // Calculate milliseconds until next minute boundary
  float msUntilNextMinute = (60.0f - currentSecond) * 1000.0f - currentMs;

  // Wake up early to account for processing time (boot to display update)
  // This is adaptively adjusted based on actual measured delay
  float processingTimeMs = rtcState.estimatedProcessingTime * 1000.0f;

  // Calculate sleep time in milliseconds, ensuring we don't go negative
  float sleepMs = msUntilNextMinute - processingTimeMs;
  if (sleepMs < 1000.0f)
  {
    // Very close to minute boundary, sleep minimal time
    sleepMs = 1000.0f;
  }

  LOGD(LogTag::DEEPSLEEP, "Current time: %d:%02d:%02d.%03d, Sleeping for %.1f sec (est. processing: %.2f sec)",
       timeinfo.tm_hour, currentMinute, currentSecond, (int)currentMs, sleepMs / 1000.0f, rtcState.estimatedProcessingTime);

  return (uint64_t)(sleepMs * 1000.0f); // Convert milliseconds to microseconds
}

void DeepSleepManager_EnterDeepSleep()
{
  uint64_t sleepDuration = DeepSleepManager_CalculateSleepDuration();

  // Save current time with microsecond precision and sleep duration to RTC
  // This prevents truncation drift (~1 minute/hour) from integer division
  struct timeval tv;
  gettimeofday(&tv, NULL);
  rtcState.savedTime = tv.tv_sec;
  rtcState.savedTimeUs = tv.tv_usec;
  rtcState.sleepDurationUs = sleepDuration;

  LOGI(LogTag::DEEPSLEEP, "Entering deep sleep for %llu seconds", sleepDuration / 1000000ULL);

  // Configure timer wakeup
  esp_sleep_enable_timer_wakeup(sleepDuration);

  // Configure GPIO wakeup for buttons
  // We use EXT0 wakeup which only supports a single pin
  // Using HOME_KEY (GPIO 2) as the wake up button
  // Note: To support multiple buttons with active-low logic on ESP32-S3/Arduino 2.x,
  // we would need ESP_EXT1_WAKEUP_ANY_LOW which is not available in this SDK version.
  // esp_deep_sleep_enable_gpio_wakeup is also not fully exposed.

  gpio_num_t wakeupPin = (gpio_num_t)2; // HOME_KEY
  esp_sleep_enable_ext0_wakeup(wakeupPin, 0); // 0 = LOW
  LOGI(LogTag::DEEPSLEEP, "GPIO wakeup enabled for HOME button (pin 2)");

  // Disable WiFi before sleep to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Disable Bluetooth if enabled
  btStop();

  // Power off SD card to save battery during deep sleep
  // SD card can consume several mA even when idle
  if (sdCardAvailable)
  {
    SD.end();                        // Unmount SD card
    digitalWrite(SD_POWER_PIN, LOW); // Cut power to SD card
    LOGD(LogTag::DEEPSLEEP, "SD card powered off for deep sleep");
  }

  // Hold EPD pins to prevent noise
  DeepSleepManager_HoldEPDPins();

  // Enter deep sleep
  Serial.flush(); // Ensure all serial output is sent before sleep
  delay(100);     // Small delay to ensure serial flush completes

  esp_deep_sleep_start();
  // Code never reaches here - ESP32 will restart after wakeup
}

uint32_t DeepSleepManager_GetBootCount()
{
  return rtcState.bootCount;
}

bool DeepSleepManager_ShouldSyncWiFiNtp()
{
  // If never synced, sync is needed
  if (rtcState.lastNtpSyncBootCount == 0)
  {
    return true;
  }

  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  uint8_t currentMinute = timeinfo.tm_min;
  uint8_t lastMinute = rtcState.lastDisplayedMinute;

  // Only sync when we're about to display a NEW hour (minute == 0)
  // This prevents double-sync: if lastDisplayedMinute == currentMinute,
  // we're waking early and will wait for minute change anyway
  bool isSyncMinute = (currentMinute == 0);
  bool isNewMinute = (lastMinute != currentMinute);

  // Case 1: Current minute is sync minute AND it's a new minute to display
  if (isSyncMinute && isNewMinute)
  {
    LOGD(LogTag::DEEPSLEEP, "Sync minute: last=%d, current=%d, triggering NTP sync",
         lastMinute, currentMinute);
    return true;
  }

  // Case 2: About to cross to sync minute (woke early, waiting for minute change)
  // lastDisplayedMinute is 59, current is same (hasn't rolled yet)
  // After waiting, minute will be 0
  if (lastMinute == 59 && currentMinute == 59)
  {
    LOGD(LogTag::DEEPSLEEP, "Hour boundary approaching: last=%d, current=%d",
         lastMinute, currentMinute);
    return true;
  }

  return false;
}

void DeepSleepManager_SaveRtcTimeBeforeSync()
{
  gettimeofday(&rtcTimeBeforeNtpSync, NULL);
  LOGD(LogTag::DEEPSLEEP, "RTC time before NTP sync: %ld.%06ld",
       (long)rtcTimeBeforeNtpSync.tv_sec, (long)rtcTimeBeforeNtpSync.tv_usec);
}

void DeepSleepManager_SaveNtpSyncDuration(unsigned long durationMs)
{
  ntpSyncDurationMs = durationMs;
  LOGD(LogTag::DEEPSLEEP, "NTP sync duration: %lu ms", durationMs);
}

void DeepSleepManager_MarkNtpSynced()
{
  rtcState.lastNtpSyncBootCount = rtcState.bootCount;

  // Get NTP time (after sync)
  struct timeval ntpTime;
  gettimeofday(&ntpTime, NULL);

  // Save previous sync time for drift rate calculation
  time_t previousNtpSyncTime = rtcState.lastNtpSyncTime;
  rtcState.lastNtpSyncTime = ntpTime.tv_sec;

  // Calculate drift in milliseconds (NTP time - RTC time)
  // Positive = RTC was behind (slow), Negative = RTC was ahead (fast)
  // Skip drift calculation on first boot or if RTC time is invalid (< year 2020)
  constexpr time_t kMinValidTime = 1577836800; // 2020-01-01 00:00:00 UTC
  if (rtcTimeBeforeNtpSync.tv_sec > kMinValidTime)
  {
    int64_t rtcMs = (int64_t)rtcTimeBeforeNtpSync.tv_sec * 1000 + rtcTimeBeforeNtpSync.tv_usec / 1000;
    int64_t ntpMs = (int64_t)ntpTime.tv_sec * 1000 + ntpTime.tv_usec / 1000;

    // Calculate raw drift (NTP time - RTC time at sync start)
    int64_t rawDriftMs = ntpMs - rtcMs;

    // Subtract NTP sync wait time - RTC continues running during this time,
    // so the wait time is not actual RTC drift
    int64_t actualDriftMs = rawDriftMs - ntpSyncDurationMs;

    rtcState.lastRtcDriftMs = (int32_t)actualDriftMs;
    rtcState.lastRtcDriftValid = true;
    LOGI(LogTag::DEEPSLEEP, "NTP synced at boot %u, residual drift: %d ms (raw: %lld ms, sync wait: %lu ms)",
         rtcState.bootCount, rtcState.lastRtcDriftMs, rawDriftMs, ntpSyncDurationMs);

    // Update drift rate using exponential moving average
    // This allows the compensation to adapt to temperature and device variations
    if (previousNtpSyncTime > kMinValidTime)
    {
      float minutesSinceSync = (float)(ntpTime.tv_sec - previousNtpSyncTime) / 60.0f;
      if (minutesSinceSync >= 1.0f)  // At least 1 minute elapsed
      {
        // Calculate TRUE drift rate by adding back the compensation we applied
        // actualDriftMs is the residual error after compensation
        // trueDrift = residual + cumulative compensation applied since last sync
        int64_t trueDriftMs = actualDriftMs + rtcState.cumulativeCompensationMs;
        float trueRate = (float)trueDriftMs / minutesSinceSync;

        LOGI(LogTag::DEEPSLEEP, "True drift: %lld ms (residual: %lld ms + compensation: %lld ms)",
             trueDriftMs, actualDriftMs, rtcState.cumulativeCompensationMs);

        // Clamp true rate to reasonable range (50-300 ms/min)
        // ESP32 RTC drift varies by temperature: ~28 ms/min at 20-22°C, ~100-200 ms/min at higher temps
        constexpr float kMinDriftRate = 20.0f;
        constexpr float kMaxDriftRate = 300.0f;
        float clampedRate = trueRate;
        if (clampedRate < kMinDriftRate) clampedRate = kMinDriftRate;
        if (clampedRate > kMaxDriftRate) clampedRate = kMaxDriftRate;

        if (trueRate != clampedRate)
        {
          LOGW(LogTag::DEEPSLEEP, "True rate %.1f ms/min clamped to %.1f ms/min",
               trueRate, clampedRate);
        }

        if (rtcState.driftRateCalibrated)
        {
          // Exponential moving average: 40% old, 60% new for faster convergence
          rtcState.driftRateMsPerMin = rtcState.driftRateMsPerMin * 0.4f + clampedRate * 0.6f;
        }
        else
        {
          // First calibration - use measured value directly
          rtcState.driftRateMsPerMin = clampedRate;
          rtcState.driftRateCalibrated = true;
        }
        LOGI(LogTag::DEEPSLEEP, "Drift rate updated: %.1f ms/min (true rate: %.1f ms/min over %.1f min)",
             rtcState.driftRateMsPerMin, trueRate, minutesSinceSync);

        // Save drift rate to SD card for persistence across power cycles
        DeepSleepManager_SaveDriftRate(rtcState.driftRateMsPerMin);
      }
    }

    // Reset cumulative compensation after NTP sync
    rtcState.cumulativeCompensationMs = 0;
  }
  else
  {
    rtcState.lastRtcDriftMs = 0;
    rtcState.lastRtcDriftValid = false;
    LOGI(LogTag::DEEPSLEEP, "NTP synced at boot %u (first sync or invalid RTC, drift skipped)", rtcState.bootCount);
  }

  // Reset sync duration for next sync
  ntpSyncDurationMs = 0;
}

bool DeepSleepManager_IsLastRtcDriftValid()
{
  return rtcState.lastRtcDriftValid;
}

int32_t DeepSleepManager_GetLastRtcDriftMs()
{
  return rtcState.lastRtcDriftMs;
}

float DeepSleepManager_GetDriftRateMsPerMin()
{
  return rtcState.driftRateMsPerMin;
}

bool DeepSleepManager_SaveFrameBuffer(const uint8_t *buffer, size_t size)
{
  File file;
  const char* storageType;

  if (sdCardAvailable)
  {
    file = SD.open(kFrameBufferFile, FILE_WRITE);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kFrameBufferFile, FILE_WRITE);
    storageType = "SPIFFS (fallback - limited write endurance)";
  }
  else
  {
    LOGE(LogTag::DEEPSLEEP, "No storage available (SD card and SPIFFS both failed)");
    return false;
  }

  if (!file)
  {
    LOGE(LogTag::DEEPSLEEP, "Failed to open file for writing on %s", storageType);
    return false;
  }

  unsigned long start = micros();
  size_t written = file.write(buffer, size);
  file.close();
  unsigned long duration = micros() - start;

  if (written == size)
  {
    rtcState.imageSize = size;
    LOGI(LogTag::DEEPSLEEP, "Saved to %s: %zu bytes in %lu us", storageType, size, duration);
    return true;
  }
  else
  {
    LOGE(LogTag::DEEPSLEEP, "Write failed (incomplete) on %s: wrote %zu of %zu", storageType, written, size);

    // Auto-recovery: If SPIFFS write fails, the filesystem might be corrupted (common after partition change)
    // Attempt to format it so it works on next boot.
    if (!sdCardAvailable && spiffsMounted)
    {
      LOGW(LogTag::DEEPSLEEP, "Detected SPIFFS corruption. Formatting SPIFFS to recover...");
      SPIFFS.end();
      if (SPIFFS.format())
      {
        LOGI(LogTag::DEEPSLEEP, "SPIFFS formatted successfully. It should work on next boot.");
        // We won't retry the write here to avoid delaying sleep too much,
        // but the filesystem is now clean for the next cycle.
      }
      else
      {
        LOGE(LogTag::DEEPSLEEP, "SPIFFS format failed!");
      }
    }

    return false;
  }
}

bool DeepSleepManager_LoadFrameBuffer(uint8_t *buffer, size_t size)
{
  if (rtcState.imageSize == 0)
  {
    LOGW(LogTag::DEEPSLEEP, "No image info found");
    return false;
  }

  File file;
  const char* storageType;
  bool fileExists = false;

  if (sdCardAvailable)
  {
    fileExists = SD.exists(kFrameBufferFile);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    fileExists = SPIFFS.exists(kFrameBufferFile);
    storageType = "SPIFFS (fallback)";
  }
  else
  {
    LOGE(LogTag::DEEPSLEEP, "No storage available (SD card and SPIFFS both failed)");
    return false;
  }

  if (!fileExists)
  {
    LOGW(LogTag::DEEPSLEEP, "Frame buffer file not found on %s", storageType);
    return false;
  }

  if (sdCardAvailable)
  {
    file = SD.open(kFrameBufferFile, FILE_READ);
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kFrameBufferFile, FILE_READ);
  }
  else
  {
    LOGE(LogTag::DEEPSLEEP, "No storage available for reading");
    return false;
  }

  if (!file)
  {
    LOGE(LogTag::DEEPSLEEP, "Failed to open file for reading on %s", storageType);
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize != rtcState.imageSize || fileSize != size)
  {
    LOGE(LogTag::DEEPSLEEP, "File size mismatch on %s: expected %zu (RTC: %u), got %zu",
         storageType, size, rtcState.imageSize, fileSize);
    file.close();
    return false;
  }

  LOGD(LogTag::DEEPSLEEP, "Loading frame buffer from %s...", storageType);
  unsigned long start = micros();
  size_t read = file.read(buffer, size);
  file.close();
  unsigned long duration = micros() - start;

  if (read == size)
  {
    LOGI(LogTag::DEEPSLEEP, "Load successful: %zu bytes in %lu us", size, duration);
    return true;
  }
  else
  {
    LOGE(LogTag::DEEPSLEEP, "Read failed (incomplete) on %s: read %zu of %zu", storageType, read, size);
    return false;
  }
}

void DeepSleepManager_HoldI2CPins()
{
  // I2C Pins for SCD41: SDA, SCL
  // We set them to INPUT_PULLUP to keep them high during deep sleep
  // This prevents glitches that might reset the sensor

  gpio_num_t sda = (gpio_num_t)SENSOR_I2C_SDA_PIN;
  gpio_num_t scl = (gpio_num_t)SENSOR_I2C_SCL_PIN;

  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);

  gpio_hold_en(sda);
  gpio_hold_en(scl);

  LOGD(LogTag::DEEPSLEEP, "I2C pins held high for deep sleep");
}

void DeepSleepManager_ReleaseI2CPins()
{
  gpio_num_t sda = (gpio_num_t)SENSOR_I2C_SDA_PIN;
  gpio_num_t scl = (gpio_num_t)SENSOR_I2C_SCL_PIN;

  gpio_hold_dis(sda);
  gpio_hold_dis(scl);

  LOGD(LogTag::DEEPSLEEP, "I2C pins hold released");
}

void DeepSleepManager_HoldEPDPins()
{
  // EPD Pins: SCK(12), MOSI(11), RES(47), DC(46), CS(45)
  // Also Pin 7 (EPD Power Enable) needs to be held to ensure stable power state
  // All should be held in safe state to prevent noise/reset/back-feeding

  // Ensure pins are in safe state before holding
  // RST: High (keep reset inactive)
  // CS: High (chip not selected)
  // DC: Low (command mode - safe default)
  // SCK: Low
  // MOSI: Low
  // Pin 7: High (Power On) - keeping power on ensures we don't back-feed signals into unpowered chip

  gpio_num_t rst = (gpio_num_t)RES;
  gpio_num_t cs = (gpio_num_t)CS;
  gpio_num_t dc = (gpio_num_t)DC;
  gpio_num_t sck = (gpio_num_t)SCK;
  gpio_num_t mosi = (gpio_num_t)MOSI;
  gpio_num_t pwr = (gpio_num_t)7;

  // Set levels explicitly
  pinMode(rst, OUTPUT);
  pinMode(cs, OUTPUT);
  pinMode(dc, OUTPUT);
  pinMode(sck, OUTPUT);
  pinMode(mosi, OUTPUT);
  pinMode(pwr, OUTPUT);

  digitalWrite(rst, HIGH);
  digitalWrite(cs, HIGH);
  digitalWrite(dc, LOW);
  digitalWrite(sck, LOW);
  digitalWrite(mosi, LOW);
  digitalWrite(pwr, HIGH);

  // Enable hold
  gpio_hold_en(rst);
  gpio_hold_en(cs);
  gpio_hold_en(dc);
  gpio_hold_en(sck);
  gpio_hold_en(mosi);
  gpio_hold_en(pwr);

  LOGD(LogTag::DEEPSLEEP, "EPD pins held for deep sleep");
}

void DeepSleepManager_ReleaseEPDPins()
{
  gpio_num_t rst = (gpio_num_t)RES;
  gpio_num_t cs = (gpio_num_t)CS;
  gpio_num_t dc = (gpio_num_t)DC;
  gpio_num_t sck = (gpio_num_t)SCK;
  gpio_num_t mosi = (gpio_num_t)MOSI;
  gpio_num_t pwr = (gpio_num_t)7;

  gpio_hold_dis(rst);
  gpio_hold_dis(cs);
  gpio_hold_dis(dc);
  gpio_hold_dis(sck);
  gpio_hold_dis(mosi);
  gpio_hold_dis(pwr);

  LOGD(LogTag::DEEPSLEEP, "EPD pins hold released");
}

bool DeepSleepManager_IsWakeFromGPIO()
{
  esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
  return (wakeupCause == ESP_SLEEP_WAKEUP_EXT0);
}

int DeepSleepManager_GetWakeupGPIO()
{
  if (!DeepSleepManager_IsWakeFromGPIO())
  {
    return -1;
  }

  // For EXT0, the wakeup pin is fixed to the one we configured
  return 2; // HOME_KEY
}

void DeepSleepManager_SaveLastUploadedTime(time_t timestamp)
{
  File file;
  const char *storageType;

  if (sdCardAvailable)
  {
    file = SD.open(kLastUploadedTimeFile, FILE_WRITE);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kLastUploadedTimeFile, FILE_WRITE);
    storageType = "SPIFFS";
  }
  else
  {
    LOGW(LogTag::DEEPSLEEP, "Cannot save lastUploadedTime: no storage available");
    return;
  }

  if (!file)
  {
    LOGW(LogTag::DEEPSLEEP, "Failed to open lastUploadedTime file for writing on %s", storageType);
    return;
  }

  file.println(timestamp);
  file.close();

  LOGD(LogTag::DEEPSLEEP, "Saved lastUploadedTime %ld to %s", (long)timestamp, storageType);
}

time_t DeepSleepManager_LoadLastUploadedTime()
{
  File file;
  const char *storageType;
  bool fileExists = false;

  if (sdCardAvailable)
  {
    fileExists = SD.exists(kLastUploadedTimeFile);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    fileExists = SPIFFS.exists(kLastUploadedTimeFile);
    storageType = "SPIFFS";
  }
  else
  {
    return 0;
  }

  if (!fileExists)
  {
    LOGD(LogTag::DEEPSLEEP, "lastUploadedTime file not found on %s", storageType);
    return 0;
  }

  if (sdCardAvailable)
  {
    file = SD.open(kLastUploadedTimeFile, FILE_READ);
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kLastUploadedTimeFile, FILE_READ);
  }
  else
  {
    return 0;
  }

  if (!file)
  {
    LOGW(LogTag::DEEPSLEEP, "Failed to open lastUploadedTime file for reading on %s", storageType);
    return 0;
  }

  String line = file.readStringUntil('\n');
  file.close();

  time_t timestamp = (time_t)line.toInt();

  if (timestamp > 0)
  {
    LOGI(LogTag::DEEPSLEEP, "Loaded lastUploadedTime %ld from %s", (long)timestamp, storageType);
  }

  return timestamp;
}

void DeepSleepManager_SaveDriftRate(float driftRate)
{
  File file;
  const char *storageType;

  if (sdCardAvailable)
  {
    file = SD.open(kDriftRateFile, FILE_WRITE);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kDriftRateFile, FILE_WRITE);
    storageType = "SPIFFS";
  }
  else
  {
    LOGW(LogTag::DEEPSLEEP, "Cannot save driftRate: no storage available");
    return;
  }

  if (!file)
  {
    LOGW(LogTag::DEEPSLEEP, "Failed to open driftRate file for writing on %s", storageType);
    return;
  }

  file.printf("%.2f\n", driftRate);
  file.close();
  LOGD(LogTag::DEEPSLEEP, "Saved driftRate %.1f to %s", driftRate, storageType);
}

float DeepSleepManager_LoadDriftRate()
{
  File file;
  const char *storageType;
  bool fileExists = false;

  if (sdCardAvailable)
  {
    fileExists = SD.exists(kDriftRateFile);
    storageType = "SD card";
  }
  else if (spiffsMounted)
  {
    fileExists = SPIFFS.exists(kDriftRateFile);
    storageType = "SPIFFS";
  }
  else
  {
    return 0;
  }

  if (!fileExists)
  {
    LOGD(LogTag::DEEPSLEEP, "driftRate file not found on %s", storageType);
    return 0;
  }

  if (sdCardAvailable)
  {
    file = SD.open(kDriftRateFile, FILE_READ);
  }
  else if (spiffsMounted)
  {
    file = SPIFFS.open(kDriftRateFile, FILE_READ);
  }
  else
  {
    return 0;
  }

  if (!file)
  {
    LOGW(LogTag::DEEPSLEEP, "Failed to open driftRate file for reading on %s", storageType);
    return 0;
  }

  String line = file.readStringUntil('\n');
  file.close();

  float driftRate = line.toFloat();

  if (driftRate > 0)
  {
    LOGI(LogTag::DEEPSLEEP, "Loaded driftRate %.1f from %s", driftRate, storageType);
  }

  return driftRate;
}
