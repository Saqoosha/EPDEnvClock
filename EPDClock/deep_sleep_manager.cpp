#include "deep_sleep_manager.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SPI.h>
#include <sys/time.h>

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

// Create an instance of SPIClass for SD card SPI communication (HSPI bus)
SPIClass SD_SPI = SPIClass(HSPI);

// We use SD card for image storage (or SPIFFS as fallback)
// SD card has much better write endurance than SPIFFS Flash memory
constexpr char kFrameBufferFile[] = "/frame.bin";
// Buffer for compression/decompression (allocated in heap/PSRAM)
uint8_t *compressionBuffer = nullptr;

bool sdCardAvailable = false;
bool initialized = false;

// Simple RLE compression for binary image
// Returns compressed size, or 0 if failed (buffer too small)
size_t compressRLE(const uint8_t *src, size_t srcSize, uint8_t *dst, size_t dstSize)
{
  size_t srcPos = 0;
  size_t dstPos = 0;

  while (srcPos < srcSize)
  {
    uint8_t current = src[srcPos];
    size_t runLength = 1;

    while (srcPos + runLength < srcSize && src[srcPos + runLength] == current && runLength < 255)
    {
      runLength++;
    }

    if (dstPos + 2 > dstSize)
    {
      return 0; // Buffer overflow
    }

    dst[dstPos++] = runLength;
    dst[dstPos++] = current;
    srcPos += runLength;
  }

  return dstPos;
}

// Decompression
bool decompressRLE(const uint8_t *src, size_t srcSize, uint8_t *dst, size_t dstSize)
{
  size_t srcPos = 0;
  size_t dstPos = 0;

  while (srcPos < srcSize)
  {
    if (srcPos + 1 >= srcSize) return false; // Invalid data

    uint8_t runLength = src[srcPos++];
    uint8_t value = src[srcPos++];

    if (dstPos + runLength > dstSize) return false; // Buffer overflow

    memset(dst + dstPos, value, runLength);
    dstPos += runLength;
  }

  return dstPos == dstSize;
}

void restoreTimeFromRTC()
{
  if (rtcState.savedTime > 0)
  {
    // Estimate elapsed time since sleep started
    // This is rough, but sleep duration is usually precise
    // We add the intended sleep duration plus a small offset for boot time
    // A better way would be using ESP32's internal RTC timer if available

    // Simple approach: savedTime + (sleepDurationUs / 1000000)
    // This assumes we slept for exactly the requested duration
    time_t wakeupTime = rtcState.savedTime + (rtcState.sleepDurationUs / 1000000) + 1; // +1 for boot overhead

    struct timeval tv;
    tv.tv_sec = wakeupTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // Re-apply timezone
    setenv("TZ", "JST-9", 1);
    tzset();

    Serial.print("[DeepSleep] Time restored: ");
    Serial.println(ctime(&wakeupTime));
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
    rtcState.compressedImageSize = 0;
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
  // Initialize HSPI bus for SD card with specified pins
  SD_SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

  // Try to mount SD card with 80MHz SPI clock speed (same as official example)
  if (SD.begin(SD_CS_PIN, SD_SPI, 80000000))
  {
    sdCardAvailable = true;
    Serial.print("[DeepSleep] SD card initialized successfully, size: ");
    Serial.print(SD.cardSize() / (1024 * 1024));
    Serial.println(" MB");
  }
  else
  {
    sdCardAvailable = false;
    Serial.println("[DeepSleep] SD card initialization failed, falling back to SPIFFS");
    Serial.println("[DeepSleep] WARNING: Using SPIFFS fallback - Flash memory write endurance is limited!");
    Serial.println("[DeepSleep] WARNING: SPIFFS has 10,000-100,000 write cycles. Consider using SD card for better durability.");

    // Fallback to SPIFFS if SD card is not available
    if (!SPIFFS.begin(true))
    {
      Serial.println("[DeepSleep] SPIFFS Mount Failed");
      Serial.println("[DeepSleep] ERROR: No storage available! Frame buffer will not be saved.");
    }
    else
    {
      Serial.println("[DeepSleep] SPIFFS Mounted (fallback)");
    }
  }

  initialized = true;

  Serial.print("[DeepSleep] Boot count: ");
  Serial.println(rtcState.bootCount);
  Serial.print("[DeepSleep] Last displayed minute: ");
  Serial.println(rtcState.lastDisplayedMinute);
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
    Serial.println("[DeepSleep] Warning: Cannot get local time, sleeping for 60 seconds");
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

  Serial.print("[DeepSleep] Current time: ");
  Serial.print(timeinfo.tm_hour);
  Serial.print(":");
  Serial.print(currentMinute);
  Serial.print(":");
  Serial.print(currentSecond);
  Serial.print(", Sleeping for ");
  Serial.print(secondsUntilNextMinute);
  Serial.println(" seconds");

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

  Serial.print("[DeepSleep] Entering deep sleep for ");
  Serial.print(sleepDuration / 1000000ULL);
  Serial.println(" seconds");

  // Configure timer wakeup
  esp_sleep_enable_timer_wakeup(sleepDuration);

  // Disable WiFi before sleep to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Disable Bluetooth if enabled
  btStop();

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

  // Check if 1 hour (60 boots) has passed since last sync
  return DeepSleepManager_ShouldResyncNtp(60);
}

void DeepSleepManager_MarkNtpSynced()
{
  rtcState.lastNtpSyncBootCount = rtcState.bootCount;
  Serial.print("[DeepSleep] NTP synced at boot count: ");
  Serial.println(rtcState.bootCount);
}

bool DeepSleepManager_SaveFrameBuffer(const uint8_t *buffer, size_t size)
{
  // Allocate buffer for compression (RLE worst case is 2x input size)
  // Fix: Allocate 2x size to prevent compression failures
  if (compressionBuffer == nullptr)
  {
    compressionBuffer = (uint8_t *)malloc(size * 2);
    if (compressionBuffer == nullptr)
    {
      Serial.println("[DeepSleep] Failed to allocate compression buffer");
      return false;
    }
  }

  unsigned long start = micros();
  size_t compressedSize = compressRLE(buffer, size, compressionBuffer, size * 2);
  unsigned long duration = micros() - start;

  if (compressedSize > 0)
  {
    File file;
    const char* storageType;

    if (sdCardAvailable)
    {
      file = SD.open(kFrameBufferFile, FILE_WRITE);
      storageType = "SD card";
    }
    else
    {
      file = SPIFFS.open(kFrameBufferFile, FILE_WRITE);
      storageType = "SPIFFS (fallback - limited write endurance)";
    }

    if (!file)
    {
      Serial.print("[DeepSleep] Failed to open file for writing on ");
      Serial.println(storageType);
      free(compressionBuffer);
      compressionBuffer = nullptr;
      return false;
    }

    size_t written = file.write(compressionBuffer, compressedSize);
    file.close();

    if (written == compressedSize)
    {
      rtcState.compressedImageSize = compressedSize;
      Serial.print("[DeepSleep] Saved to ");
      Serial.print(storageType);
      Serial.print(": ");
      Serial.print(size);
      Serial.print(" -> ");
      Serial.print(compressedSize);
      Serial.print(" bytes (");
      Serial.print((float)compressedSize / size * 100.0);
      Serial.print("%) in ");
      Serial.print(duration);
      Serial.println(" us");

      free(compressionBuffer);
      compressionBuffer = nullptr;
      return true;
    }
    else
    {
      Serial.print("[DeepSleep] Write failed (incomplete) on ");
      Serial.println(storageType);
      free(compressionBuffer);
      compressionBuffer = nullptr;
      return false;
    }
  }
  else
  {
    rtcState.compressedImageSize = 0;
    Serial.println("[DeepSleep] Compression failed (buffer overflow)");
    free(compressionBuffer);
    compressionBuffer = nullptr;
    return false;
  }
}

bool DeepSleepManager_LoadFrameBuffer(uint8_t *buffer, size_t size)
{
  if (rtcState.compressedImageSize == 0)
  {
    Serial.println("[DeepSleep] No compressed image info found");
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
  else
  {
    fileExists = SPIFFS.exists(kFrameBufferFile);
    storageType = "SPIFFS (fallback)";
  }

  if (!fileExists)
  {
    Serial.print("[DeepSleep] Frame buffer file not found on ");
    Serial.println(storageType);
    return false;
  }

  if (sdCardAvailable)
  {
    file = SD.open(kFrameBufferFile, FILE_READ);
  }
  else
  {
    file = SPIFFS.open(kFrameBufferFile, FILE_READ);
  }

  if (!file)
  {
    Serial.print("[DeepSleep] Failed to open file for reading on ");
    Serial.println(storageType);
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize != rtcState.compressedImageSize)
  {
    Serial.print("[DeepSleep] File size mismatch on ");
    Serial.print(storageType);
    Serial.print(": expected ");
    Serial.print(rtcState.compressedImageSize);
    Serial.print(", got ");
    Serial.println(fileSize);
    file.close();
    return false;
  }

  // Allocate buffer for reading compressed data
  if (compressionBuffer == nullptr)
  {
    compressionBuffer = (uint8_t *)malloc(fileSize);
    if (compressionBuffer == nullptr)
    {
      Serial.println("[DeepSleep] Failed to allocate decompression buffer");
      file.close();
      return false;
    }
  }

  size_t read = file.read(compressionBuffer, fileSize);
  file.close();

  if (read != fileSize)
  {
    Serial.print("[DeepSleep] Read failed (incomplete) on ");
    Serial.println(storageType);
    free(compressionBuffer);
    compressionBuffer = nullptr;
    return false;
  }

  Serial.print("[DeepSleep] Decompressing frame buffer from ");
  Serial.print(storageType);
  Serial.println("...");
  unsigned long start = micros();
  bool success = decompressRLE(compressionBuffer, fileSize, buffer, size);
  unsigned long duration = micros() - start;

  free(compressionBuffer);
  compressionBuffer = nullptr;

  if (success)
  {
    Serial.print("[DeepSleep] Decompression successful in ");
    Serial.print(duration);
    Serial.println(" us");
    return true;
  }
  else
  {
    Serial.println("[DeepSleep] Decompression failed");
    return false;
  }
}

void DeepSleepManager_HoldI2CPins()
{
  // I2C Pins for SDC41: SDA=38, SCL=21
  // We set them to INPUT_PULLUP to keep them high during deep sleep
  // This prevents glitches that might reset the sensor

  gpio_num_t sda = (gpio_num_t)38;
  gpio_num_t scl = (gpio_num_t)21;

  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);

  gpio_hold_en(sda);
  gpio_hold_en(scl);

  Serial.println("[DeepSleep] I2C pins held high for deep sleep");
}

void DeepSleepManager_ReleaseI2CPins()
{
  gpio_num_t sda = (gpio_num_t)38;
  gpio_num_t scl = (gpio_num_t)21;

  gpio_hold_dis(sda);
  gpio_hold_dis(scl);

  Serial.println("[DeepSleep] I2C pins hold released");
}
