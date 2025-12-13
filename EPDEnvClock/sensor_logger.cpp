#include "sensor_logger.h"
#include "deep_sleep_manager.h"
#include "logger.h"

#include <SPI.h>
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
// batt_percent: linear percent (3.4V=0%, 4.2V=100%)
// batt_max17048_percent: MAX17048 reported percent (for reference)
void formatJSONLine(const struct tm &timeinfo,
                    time_t unixTimestamp,
                    int32_t rtcDriftMs,
                    bool ntpSynced,
                    float temperature,
                    float humidity,
                    uint16_t co2,
                    float batteryVoltage,
                    float batteryPercent,
                    float batteryMax17048Percent,
                    float batteryChargeRate,
                    bool batteryCharging,
                    char *buffer,
                    size_t bufferSize)
{
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int second = timeinfo.tm_sec;

  // Format battery fields - use null for invalid values (-1.0)
  char battVoltageStr[16];
  char battPercentStr[16];
  char battMax17048Str[16];
  char battRateStr[16];
  
  if (batteryVoltage < 0.0f) {
    snprintf(battVoltageStr, sizeof(battVoltageStr), "null");
    snprintf(battPercentStr, sizeof(battPercentStr), "null");
    snprintf(battMax17048Str, sizeof(battMax17048Str), "null");
    snprintf(battRateStr, sizeof(battRateStr), "null");
  } else {
    snprintf(battVoltageStr, sizeof(battVoltageStr), "%.3f", batteryVoltage);
    snprintf(battPercentStr, sizeof(battPercentStr), "%.1f", batteryPercent);
    snprintf(battMax17048Str, sizeof(battMax17048Str), "%.1f", batteryMax17048Percent);
    snprintf(battRateStr, sizeof(battRateStr), "%.2f", batteryChargeRate);
  }

  if (ntpSynced)
  {
    // Include RTC drift when NTP was synced this boot
    snprintf(buffer, bufferSize,
             "{\"date\":\"%04d.%02d.%02d\",\"time\":\"%02d:%02d:%02d\",\"unixtimestamp\":%ld,\"rtc_drift_ms\":%d,\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%u,\"batt_voltage\":%s,\"batt_percent\":%s,\"batt_max17048_percent\":%s,\"batt_rate\":%s,\"charging\":%s}\n",
             year, month, day,
             hour, minute, second,
             (long)unixTimestamp,
             rtcDriftMs,
             temperature, humidity, co2,
             battVoltageStr, battPercentStr, battMax17048Str, battRateStr,
             batteryCharging ? "true" : "false");
  }
  else
  {
    // No drift data when NTP wasn't synced this boot
    snprintf(buffer, bufferSize,
             "{\"date\":\"%04d.%02d.%02d\",\"time\":\"%02d:%02d:%02d\",\"unixtimestamp\":%ld,\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%u,\"batt_voltage\":%s,\"batt_percent\":%s,\"batt_max17048_percent\":%s,\"batt_rate\":%s,\"charging\":%s}\n",
             year, month, day,
             hour, minute, second,
             (long)unixTimestamp,
             temperature, humidity, co2,
             battVoltageStr, battPercentStr, battMax17048Str, battRateStr,
             batteryCharging ? "true" : "false");
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

  // Clean up old log files (older than 30 days)
  SensorLogger_DeleteOldFiles(30);
}

bool SensorLogger_LogValues(
    const struct tm &timeinfo,
    time_t unixTimestamp,
    int32_t rtcDriftMs,
    bool ntpSynced,
    float temperature,
    float humidity,
    uint16_t co2,
    float batteryVoltage,
    float batteryPercent,
    float batteryMax17048Percent,
    float batteryChargeRate,
    bool batteryCharging)
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

  // Format JSON line (increased buffer for new field)
  char jsonLine[384];
  formatJSONLine(timeinfo, unixTimestamp, rtcDriftMs, ntpSynced, temperature, humidity, co2,
                 batteryVoltage, batteryPercent, batteryMax17048Percent, batteryChargeRate, batteryCharging,
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

int SensorLogger_DeleteOldFiles(int maxAgeDays)
{
  if (!sdCardAvailable)
  {
    return 0;
  }

  // Get current time
  time_t now;
  time(&now);
  struct tm currentTime;
  localtime_r(&now, &currentTime);

  // Calculate cutoff date (maxAgeDays ago)
  time_t cutoffTime = now - (maxAgeDays * 24 * 60 * 60);

  File dir = SD.open(kLogDirectory);
  if (!dir || !dir.isDirectory())
  {
    LOGW(LogTag::SENSOR, "Sensor logger: Cannot open log directory for cleanup");
    return 0;
  }

  int deletedCount = 0;
  File entry;

  while ((entry = dir.openNextFile()))
  {
    const char *name = entry.name();
    entry.close();

    // Check if filename matches pattern: sensor_log_YYYYMMDD.jsonl
    // name() returns just the filename without path
    if (strncmp(name, "sensor_log_", 11) != 0)
    {
      continue;
    }

    // Parse date from filename: sensor_log_YYYYMMDD.jsonl
    int year, month, day;
    if (sscanf(name, "sensor_log_%4d%2d%2d.jsonl", &year, &month, &day) != 3)
    {
      continue;
    }

    // Convert to time_t for comparison
    struct tm fileTime = {0};
    fileTime.tm_year = year - 1900;
    fileTime.tm_mon = month - 1;
    fileTime.tm_mday = day;
    fileTime.tm_hour = 12; // Noon to avoid DST issues
    time_t fileTimestamp = mktime(&fileTime);

    // Delete if older than cutoff
    if (fileTimestamp < cutoffTime)
    {
      char fullPath[kMaxFilenameLength];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", kLogDirectory, name);

      if (SD.remove(fullPath))
      {
        LOGI(LogTag::SENSOR, "Sensor logger: Deleted old log file %s", name);
        deletedCount++;
      }
      else
      {
        LOGW(LogTag::SENSOR, "Sensor logger: Failed to delete %s", name);
      }
    }
  }

  dir.close();

  if (deletedCount > 0)
  {
    LOGI(LogTag::SENSOR, "Sensor logger: Deleted %d old log files (>%d days)", deletedCount, maxAgeDays);
  }

  return deletedCount;
}

int SensorLogger_GetUnsentReadings(time_t lastUploadedTime, String &payload, time_t &latestTimestamp, int maxReadings)
{
  if (!initialized || !sdCardAvailable)
  {
    return 0;
  }

  // Get current time
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Use a circular buffer to keep only the LATEST maxReadings entries
  // Default is 120 readings (2 hours) to handle temporary upload failures gracefully
  String *entries = new String[maxReadings];
  time_t *timestamps = new time_t[maxReadings];
  int bufferCount = 0;
  int bufferStart = 0; // Index of oldest entry in circular buffer

  // Helper lambda to process a log file
  auto processFile = [&](const char *filename) {
    if (!SD.exists(filename))
    {
      return;
    }

    File file = SD.open(filename, FILE_READ);
    if (!file)
    {
      return;
    }

    while (file.available())
    {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      // Parse timestamp from JSON line
      // Format: {"date":"...","unixtimestamp":1234567890,...}
      int tsIndex = line.indexOf("\"unixtimestamp\":");
      if (tsIndex > 0)
      {
        int start = tsIndex + 16; // length of "unixtimestamp":
        int end = line.indexOf(",", start);
        if (end > start)
        {
          String tsStr = line.substring(start, end);
          time_t ts = (time_t)tsStr.toInt();

          // Only include entries newer than lastUploadedTime
          if (ts > lastUploadedTime)
          {
            if (bufferCount < maxReadings)
            {
              // Buffer not full yet, just add
              int idx = (bufferStart + bufferCount) % maxReadings;
              entries[idx] = line;
              timestamps[idx] = ts;
              bufferCount++;
            }
            else
            {
              // Buffer full, overwrite oldest entry (circular)
              entries[bufferStart] = line;
              timestamps[bufferStart] = ts;
              bufferStart = (bufferStart + 1) % maxReadings;
            }
          }
        }
      }
    }
    file.close();
  };

  // Check yesterday's file first (in case there's recent data spanning midnight)
  time_t yesterday = now - 86400;
  struct tm tmYesterday;
  localtime_r(&yesterday, &tmYesterday);

  char filenameYesterday[kMaxFilenameLength];
  generateLogFilename(tmYesterday, filenameYesterday, sizeof(filenameYesterday));

  processFile(filenameYesterday);

  // Check today's file
  char filenameToday[kMaxFilenameLength];
  generateLogFilename(timeinfo, filenameToday, sizeof(filenameToday));
  processFile(filenameToday);

  // Build payload from buffer (in chronological order)
  payload += "[";
  latestTimestamp = lastUploadedTime;

  for (int i = 0; i < bufferCount; i++)
  {
    int idx = (bufferStart + i) % maxReadings;
    if (i > 0)
    {
      payload += ",";
    }
    payload += entries[idx];

    if (timestamps[idx] > latestTimestamp)
    {
      latestTimestamp = timestamps[idx];
    }
  }

  payload += "]";

  // Clean up
  delete[] entries;
  delete[] timestamps;

  return bufferCount;
}
