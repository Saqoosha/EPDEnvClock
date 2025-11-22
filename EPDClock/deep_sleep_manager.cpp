#include "deep_sleep_manager.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>
#include <SPIFFS.h>
#include <sys/time.h>

namespace
{
// RTC memory attribute ensures this persists across deep sleep
RTC_DATA_ATTR RTCState rtcState;

// We use SPIFFS for image storage instead of RTC memory due to size constraints
constexpr char kFrameBufferFile[] = "/frame.bin";
// Buffer for compression/decompression (allocated in heap/PSRAM)
uint8_t *compressionBuffer = nullptr;

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

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
      Serial.println("[DeepSleep] SPIFFS Mount Failed");
    }
    else
    {
      Serial.println("[DeepSleep] SPIFFS Mounted");
    }
  }
  else
  {
    // subsequent boots
    if (!SPIFFS.begin(true))
    {
      Serial.println("[DeepSleep] SPIFFS Mount Failed");
    }

    // Restore time if not syncing via NTP immediately
    // But we only know if we sync NTP later.
    // It's safe to restore it now; NTP will overwrite it if needed.
    restoreTimeFromRTC();
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
  Serial.println("[DeepSleep] Saving frame buffer to SPIFFS...");

  // Allocate buffer for compression (max size same as input to be safe)
  if (compressionBuffer == nullptr)
  {
    compressionBuffer = (uint8_t *)malloc(size);
    if (compressionBuffer == nullptr)
    {
      Serial.println("[DeepSleep] Failed to allocate compression buffer");
      return false;
    }
  }

  unsigned long start = micros();
  size_t compressedSize = compressRLE(buffer, size, compressionBuffer, size);
  unsigned long duration = micros() - start;

  if (compressedSize > 0)
  {
    File file = SPIFFS.open(kFrameBufferFile, FILE_WRITE);
    if (!file)
    {
      Serial.println("[DeepSleep] Failed to open file for writing");
      free(compressionBuffer);
      compressionBuffer = nullptr;
      return false;
    }

    size_t written = file.write(compressionBuffer, compressedSize);
    file.close();

    if (written == compressedSize)
    {
      rtcState.compressedImageSize = compressedSize;
      Serial.print("[DeepSleep] Saved: ");
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
      Serial.println("[DeepSleep] Write failed (incomplete)");
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

  if (!SPIFFS.exists(kFrameBufferFile))
  {
    Serial.println("[DeepSleep] Frame buffer file not found");
    return false;
  }

  File file = SPIFFS.open(kFrameBufferFile, FILE_READ);
  if (!file)
  {
    Serial.println("[DeepSleep] Failed to open file for reading");
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize != rtcState.compressedImageSize)
  {
    Serial.println("[DeepSleep] File size mismatch");
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
    Serial.println("[DeepSleep] Read failed (incomplete)");
    free(compressionBuffer);
    compressionBuffer = nullptr;
    return false;
  }

  Serial.println("[DeepSleep] Decompressing frame buffer...");
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
