#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <SD.h>
#include <sys/time.h>

namespace
{
LoggerConfig g_config;

// Buffer for ERROR/WARN logs to be written to SD card
constexpr size_t kMaxLogEntries = 32;
constexpr size_t kMaxLogLineLength = 192;
constexpr char kLogDirectory[] = "/error_logs";

struct LogEntry
{
  char message[kMaxLogLineLength];
  bool used;
};

LogEntry g_logBuffer[kMaxLogEntries];
size_t g_logBufferIndex = 0;
bool g_sdLoggingEnabled = false;

void bufferLogForSD(const char *formattedLog)
{
  if (g_logBufferIndex < kMaxLogEntries)
  {
    strncpy(g_logBuffer[g_logBufferIndex].message, formattedLog, kMaxLogLineLength - 1);
    g_logBuffer[g_logBufferIndex].message[kMaxLogLineLength - 1] = '\0';
    g_logBuffer[g_logBufferIndex].used = true;
    g_logBufferIndex++;
  }
  // If buffer is full, oldest logs are lost (ring buffer would add complexity)
}

const char *getLevelString(LogLevel level)
{
  switch (level)
  {
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARN:
    return "WARN";
  case LogLevel::ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

const char *getLevelColor(LogLevel level)
{
  if (!g_config.enableColors)
  {
    return "";
  }

  switch (level)
  {
  case LogLevel::DEBUG:
    return "\033[36m"; // Cyan
  case LogLevel::INFO:
    return "\033[32m"; // Green
  case LogLevel::WARN:
    return "\033[33m"; // Yellow
  case LogLevel::ERROR:
    return "\033[31m"; // Red
  default:
    return "";
  }
}

const char *getResetColor()
{
  return g_config.enableColors ? "\033[0m" : "";
}

void formatBootTime(char *buffer, size_t bufferSize)
{
  unsigned long ms = millis();
  snprintf(buffer, bufferSize, "%lums", ms);
}

void formatDateTime(char *buffer, size_t bufferSize)
{
  // NOTE:
  // We want a real fractional second here. Using millis()%1000 mixes boot-time with wall-clock
  // and can appear to go backwards when system time is corrected/restored.
  struct timeval tv;
  gettimeofday(&tv, NULL);

  time_t sec = (time_t)tv.tv_sec;
  struct tm timeinfo;
  if (localtime_r(&sec, &timeinfo) == nullptr)
  {
    snprintf(buffer, bufferSize, "N/A");
    return;
  }

  const unsigned long ms = (unsigned long)(tv.tv_usec / 1000);
  snprintf(buffer, bufferSize, "%04d-%02d-%02d %02d:%02d:%02d.%03lu",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec,
           ms);
}

void printTimestamp()
{
  char bootTimeBuffer[32];
  char dateTimeBuffer[32];

  switch (g_config.timestampMode)
  {
  case TimestampMode::BOOT_TIME:
    formatBootTime(bootTimeBuffer, sizeof(bootTimeBuffer));
    Serial.print("[");
    Serial.print(bootTimeBuffer);
    Serial.print("]");
    break;

  case TimestampMode::DATE_TIME:
    if (g_config.ntpSynced)
    {
      formatDateTime(dateTimeBuffer, sizeof(dateTimeBuffer));
      Serial.print("[");
      Serial.print(dateTimeBuffer);
      Serial.print("]");
    }
    else
    {
      formatBootTime(bootTimeBuffer, sizeof(bootTimeBuffer));
      Serial.print("[");
      Serial.print(bootTimeBuffer);
      Serial.print("]");
    }
    break;

  case TimestampMode::BOTH:
    formatBootTime(bootTimeBuffer, sizeof(bootTimeBuffer));
    Serial.print("[");
    Serial.print(bootTimeBuffer);
    if (g_config.ntpSynced)
    {
      formatDateTime(dateTimeBuffer, sizeof(dateTimeBuffer));
      Serial.print(" | ");
      Serial.print(dateTimeBuffer);
    }
    Serial.print("]");
    break;
  }
}

} // namespace

void Logger_Init(LogLevel minLevel, TimestampMode timestampMode)
{
  g_config.minLevel = minLevel;
  g_config.timestampMode = timestampMode;
  g_config.enableColors = true;
  g_config.ntpSynced = false;
}

void Logger_SetMinLevel(LogLevel level)
{
  g_config.minLevel = level;
}

void Logger_SetTimestampMode(TimestampMode mode)
{
  g_config.timestampMode = mode;
}

void Logger_SetNtpSynced(bool synced)
{
  g_config.ntpSynced = synced;
}

void Logger_Log(LogLevel level, const char *tag, const char *format, ...)
{
  // Check if log level is enabled
  if (level < g_config.minLevel)
  {
    return;
  }

  // Format the user message first
  va_list args;
  va_start(args, format);
  char messageBuffer[256];
  vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
  va_end(args);

  // Print timestamp
  printTimestamp();

  // Print level with color
  Serial.print(" ");
  Serial.print(getLevelColor(level));
  Serial.print("[");
  Serial.print(getLevelString(level));
  Serial.print("]");
  Serial.print(getResetColor());

  // Print tag
  Serial.print(" [");
  Serial.print(tag);
  Serial.print("]");

  // Print message
  Serial.print(" ");
  Serial.print(messageBuffer);
  Serial.println();

  // Buffer ERROR and WARN logs for SD card
  if (level >= LogLevel::WARN)
  {
    char logLine[kMaxLogLineLength];
    char timestampBuffer[64];

    // Format timestamp for SD log (no colors)
    // Try to get actual date/time first (works if RTC time was restored from deep sleep)
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      char bootTimeBuffer[16];
      formatBootTime(bootTimeBuffer, sizeof(bootTimeBuffer));
      // Include both datetime and boot time for correlation
      snprintf(timestampBuffer, sizeof(timestampBuffer), "%04d-%02d-%02d %02d:%02d:%02d (%s)",
               timeinfo.tm_year + 1900,
               timeinfo.tm_mon + 1,
               timeinfo.tm_mday,
               timeinfo.tm_hour,
               timeinfo.tm_min,
               timeinfo.tm_sec,
               bootTimeBuffer);
    }
    else
    {
      formatBootTime(timestampBuffer, sizeof(timestampBuffer));
    }

    snprintf(logLine, sizeof(logLine), "[%s] [%s] [%s] %s",
             timestampBuffer,
             getLevelString(level),
             tag,
             messageBuffer);

    bufferLogForSD(logLine);
  }
}

int Logger_FlushToSD()
{
  // Check if there are any logs to write
  if (g_logBufferIndex == 0)
  {
    return 0;
  }

  // Check if SD card is available
  if (!SD.exists("/"))
  {
    Serial.println("[Logger] SD card not available, cannot flush logs");
    return -1;
  }

  // Create log directory if needed
  if (!SD.exists(kLogDirectory))
  {
    if (!SD.mkdir(kLogDirectory))
    {
      Serial.println("[Logger] Failed to create log directory");
      return -1;
    }
  }

  // Generate filename with current date
  char filename[64];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    snprintf(filename, sizeof(filename), "%s/error_%04d%02d%02d.log",
             kLogDirectory,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);
  }
  else
  {
    // Fallback if time not available
    snprintf(filename, sizeof(filename), "%s/error_unknown.log", kLogDirectory);
  }

  // Open file for append
  File file = SD.open(filename, FILE_APPEND);
  if (!file)
  {
    Serial.printf("[Logger] Failed to open log file: %s\n", filename);
    return -1;
  }

  // Write all buffered logs
  int written = 0;
  for (size_t i = 0; i < g_logBufferIndex; i++)
  {
    if (g_logBuffer[i].used)
    {
      file.println(g_logBuffer[i].message);
      g_logBuffer[i].used = false;
      written++;
    }
  }

  file.close();

  // Clear buffer
  g_logBufferIndex = 0;

  Serial.printf("[Logger] Flushed %d log entries to %s\n", written, filename);
  return written;
}
