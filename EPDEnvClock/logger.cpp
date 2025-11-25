#include "logger.h"

#include <stdarg.h>
#include <stdio.h>

namespace
{
LoggerConfig g_config;

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
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    snprintf(buffer, bufferSize, "N/A");
    return;
  }

  unsigned long ms = millis() % 1000;
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

  va_list args;
  va_start(args, format);

  // Use vsnprintf to format the message
  char messageBuffer[256];
  vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
  Serial.print(messageBuffer);

  va_end(args);

  Serial.println();
}
