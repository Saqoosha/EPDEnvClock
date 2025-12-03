#pragma once

#include <Arduino.h>
#include <time.h>

// Log levels
enum class LogLevel : uint8_t
{
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3
};

// Log tags
namespace LogTag
{
  constexpr const char *SETUP = "Setup";
  constexpr const char *LOOP = "Loop";
  constexpr const char *NETWORK = "Network";
  constexpr const char *SENSOR = "Sensor";
  constexpr const char *DISPLAY_MGR = "Display";  // DISPLAY conflicts with Arduino.h macro
  constexpr const char *FONT = "Font";
  constexpr const char *DEEPSLEEP = "DeepSleep";
  constexpr const char *IMAGEBW = "ImageBW";
}

// Timestamp mode
enum class TimestampMode : uint8_t
{
  BOOT_TIME = 0,      // Show time from boot (millis())
  DATE_TIME = 1,      // Show actual date/time (requires NTP sync)
  BOTH = 2            // Show both boot time and date/time
};

// Logger configuration
struct LoggerConfig
{
  LogLevel minLevel = LogLevel::DEBUG;  // Minimum log level to output
  TimestampMode timestampMode = TimestampMode::BOTH;  // Timestamp display mode
  bool enableColors = true;             // Enable ANSI color codes
  bool ntpSynced = false;               // Whether NTP is synced (for date/time)
};

// Initialize logger
void Logger_Init(LogLevel minLevel = LogLevel::DEBUG, TimestampMode timestampMode = TimestampMode::BOTH);

// Set minimum log level
void Logger_SetMinLevel(LogLevel level);

// Set timestamp mode
void Logger_SetTimestampMode(TimestampMode mode);

// Update NTP sync status (call after NTP sync)
void Logger_SetNtpSynced(bool synced);

// Log functions
void Logger_Log(LogLevel level, const char *tag, const char *format, ...);

// Flush buffered ERROR/WARN logs to SD card
// Call this before deep sleep to persist error logs
// Returns number of log entries written, or -1 on error
int Logger_FlushToSD();

// Convenience macros
#define LOG_DEBUG(tag, ...) Logger_Log(LogLevel::DEBUG, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) Logger_Log(LogLevel::INFO, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) Logger_Log(LogLevel::WARN, tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) Logger_Log(LogLevel::ERROR, tag, __VA_ARGS__)

// Compile-time log level filtering (set LOG_MIN_LEVEL to disable logs at compile time)
#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL 0  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
#endif

#if LOG_MIN_LEVEL <= 0
#define LOGD(tag, ...) LOG_DEBUG(tag, __VA_ARGS__)
#else
#define LOGD(tag, ...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 1
#define LOGI(tag, ...) LOG_INFO(tag, __VA_ARGS__)
#else
#define LOGI(tag, ...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 2
#define LOGW(tag, ...) LOG_WARN(tag, __VA_ARGS__)
#else
#define LOGW(tag, ...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 3
#define LOGE(tag, ...) LOG_ERROR(tag, __VA_ARGS__)
#else
#define LOGE(tag, ...) ((void)0)
#endif
