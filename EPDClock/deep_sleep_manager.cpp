#include "deep_sleep_manager.h"
#include "spi.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <time.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SPI.h>
#include <sys/time.h>
#include "logger.h"
#include "sensor_manager.h"

namespace
{
// RTC memory attribute ensures this persists across deep sleep
RTC_DATA_ATTR RTCState rtcState;

// SD card pin configuration (SPI interface)
// Using HSPI bus (separate from EPD display which uses bit-banging SPI)
// Based on official 5.79" E-Paper example: /path/to/Downloads/Examples/5.79_TF/5.79_TF.ino
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

bool sdCardAvailable = false;
bool spiffsMounted = false;
bool initialized = false;

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

    // Calculate wakeup time: savedTime + sleepDuration + bootOverhead
    time_t wakeupTime = rtcState.savedTime + (rtcState.sleepDurationUs / 1000000) + (bootOverheadUs / 1000000);

    struct timeval tv;
    tv.tv_sec = wakeupTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // Re-apply timezone
    setenv("TZ", "JST-9", 1);
    tzset();

    // ctime() includes newline, so we format manually
    struct tm *timeinfo = localtime(&wakeupTime);
    LOGI(LogTag::DEEPSLEEP, "Time restored: %04d-%02d-%02d %02d:%02d:%02d (boot overhead: %lld ms)",
         timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
         bootOverheadUs / 1000);
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

  // Validate RTC state
  if (rtcState.magic != 0xDEADBEEF)
  {
    // First boot or invalid RTC data - reset state
    rtcState.magic = 0xDEADBEEF;
    rtcState.lastDisplayedMinute = 255;
    rtcState.sensorInitialized = false;
    rtcState.bootCount = 1;
    rtcState.lastNtpSyncBootCount = 0;
    rtcState.imageSize = 0;
    rtcState.savedTime = 0;
    rtcState.sleepDurationUs = 0;
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

  // Calculate seconds until next minute
  uint32_t secondsUntilNextMinute = 60 - currentSecond;

  // Add a small buffer (2 seconds) to ensure we wake up before the minute changes
  // This gives us time to initialize and update the display
  if (secondsUntilNextMinute > 2)
  {
    secondsUntilNextMinute -= 2;
  }
  else
  {
    // If we're very close to the minute boundary, sleep for a shorter time
    secondsUntilNextMinute = 1;
  }

  LOGD(LogTag::DEEPSLEEP, "Current time: %d:%02d:%02d, Sleeping for %u seconds",
       timeinfo.tm_hour, currentMinute, currentSecond, secondsUntilNextMinute);

  return secondsUntilNextMinute * 1000000ULL; // Convert to microseconds
}

void DeepSleepManager_EnterDeepSleep()
{
  uint64_t sleepDuration = DeepSleepManager_CalculateSleepDuration();

  // Save current time and sleep duration to RTC
  time_t now;
  time(&now);
  rtcState.savedTime = now;
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

bool DeepSleepManager_ShouldResyncNtp(uint32_t intervalBoots)
{
  // Check if enough boots have passed since last NTP sync
  uint32_t bootsSinceLastSync = rtcState.bootCount - rtcState.lastNtpSyncBootCount;
  return bootsSinceLastSync >= intervalBoots;
}

bool DeepSleepManager_ShouldSyncWiFiNtp()
{
  // Check if WiFi/NTP sync is needed (1 hour = ~60 boots)
  // If lastNtpSyncBootCount is 0, it means never synced, so sync is needed
  if (rtcState.lastNtpSyncBootCount == 0)
  {
    return true;
  }

  // Check if sync interval has passed since last sync
  return DeepSleepManager_ShouldResyncNtp(kNtpSyncIntervalBoots);
}

void DeepSleepManager_MarkNtpSynced()
{
  rtcState.lastNtpSyncBootCount = rtcState.bootCount;
  LOGI(LogTag::DEEPSLEEP, "NTP synced at boot count: %u", rtcState.bootCount);
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
