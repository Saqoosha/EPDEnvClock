#include "sensor_logger.h"
#include "deep_sleep_manager.h"
#include "logger.h"

#include <SD.h>

namespace
{
constexpr char kLogDirectory[] = "/sensor_logs";
constexpr size_t kMaxFilenameLength = 64;
bool initialized = false;
bool sdCardAvailable = false;

// Check if SD card is available
// SD card should already be initialized by DeepSleepManager_Init()
bool checkSDCardAvailable()
{
  // Check if SD card is already initialized and accessible
  if (SD.exists("/"))
  {
    sdCardAvailable = true;
    return true;
  }

  sdCardAvailable = false;
  return false;
}

// Generate filename from date: /sensor_logs/sensor_log_YYYYMMDD.jsonl
void generateLogFilename(const struct tm &timeinfo, char *filename, size_t maxLen)
{
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;

  snprintf(filename, maxLen, "%s/sensor_log_%04d%02d%02d.jsonl",
           kLogDirectory, year, month, day);
}

// Format JSON line
// If NTP synced this boot, includes rtc_drift_ms field
void formatJSONLine(const struct tm &timeinfo,
                    time_t unixTimestamp,
                    int32_t rtcDriftMs,
                    bool ntpSynced,
                    float temperature,
                    float humidity,
                    uint16_t co2,
                    int batteryRawADC,
                    float batteryVoltage,
                    char *buffer,
                    size_t bufferSize)
{
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int second = timeinfo.tm_sec;

  if (ntpSynced)
  {
    // Include RTC drift when NTP was synced this boot
    snprintf(buffer, bufferSize,
             "{\"date\":\"%04d.%02d.%02d\",\"time\":\"%02d:%02d:%02d\",\"unixtimestamp\":%ld,\"rtc_drift_ms\":%d,\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%u,\"batt_adc\":%d,\"batt_voltage\":%.3f}\n",
             year, month, day,
             hour, minute, second,
             (long)unixTimestamp,
             rtcDriftMs,
             temperature, humidity, co2,
             batteryRawADC, batteryVoltage);
  }
  else
  {
    // No drift data when NTP wasn't synced this boot
    snprintf(buffer, bufferSize,
             "{\"date\":\"%04d.%02d.%02d\",\"time\":\"%02d:%02d:%02d\",\"unixtimestamp\":%ld,\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%u,\"batt_adc\":%d,\"batt_voltage\":%.3f}\n",
             year, month, day,
             hour, minute, second,
             (long)unixTimestamp,
             temperature, humidity, co2,
             batteryRawADC, batteryVoltage);
  }
}

} // namespace

void SensorLogger_Init()
{
  if (initialized)
  {
    return;
  }

  // Check if SD card is available
  if (!checkSDCardAvailable())
  {
    LOGD(LogTag::SENSOR, "Sensor logger: SD card not available, logging disabled");
    initialized = true; // Mark as initialized to avoid repeated messages
    return;
  }

  // Create log directory if it doesn't exist
  if (!SD.exists(kLogDirectory))
  {
    if (SD.mkdir(kLogDirectory))
    {
      LOGI(LogTag::SENSOR, "Sensor logger: Created directory %s", kLogDirectory);
    }
    else
    {
      LOGW(LogTag::SENSOR, "Sensor logger: Failed to create directory %s", kLogDirectory);
    }
  }

  initialized = true;
  LOGI(LogTag::SENSOR, "Sensor logger initialized (SD card)");
}

bool SensorLogger_LogValues(
    const struct tm &timeinfo,
    time_t unixTimestamp,
    int32_t rtcDriftMs,
    bool ntpSynced,
    float temperature,
    float humidity,
    uint16_t co2,
    int batteryRawADC,
    float batteryVoltage)
{
  if (!initialized)
  {
    LOGD(LogTag::SENSOR, "Sensor logger not initialized");
    return false;
  }

  if (!sdCardAvailable)
  {
    // SD card not available, silently skip logging
    return false;
  }

  // Generate filename
  char filename[kMaxFilenameLength];
  generateLogFilename(timeinfo, filename, sizeof(filename));

  // Format JSON line
  char jsonLine[320];
  formatJSONLine(timeinfo, unixTimestamp, rtcDriftMs, ntpSynced, temperature, humidity, co2, batteryRawADC, batteryVoltage,
                 jsonLine, sizeof(jsonLine));

  // Open file for append
  File file = SD.open(filename, FILE_APPEND);
  if (!file)
  {
    LOGE(LogTag::SENSOR, "Sensor logger: Failed to open file %s", filename);
    return false;
  }

  // Write JSON line
  size_t written = file.print(jsonLine);
  file.close();

  if (written == strlen(jsonLine))
  {
    LOGD(LogTag::SENSOR, "Sensor logger: Logged to %s (%zu bytes)", filename, written);
    return true;
  }
  else
  {
    LOGE(LogTag::SENSOR, "Sensor logger: Write incomplete (wrote %zu of %zu)", written, strlen(jsonLine));
    return false;
  }
}
