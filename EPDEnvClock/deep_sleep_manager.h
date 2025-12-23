#pragma once

#include <Arduino.h>
#include <sys/time.h>

// RTC memory structure to persist across deep sleep
// Default RTC drift rate measured on this device (Dec 2025)
// RTC slow clock runs at ~143.69 kHz instead of nominal 150 kHz
// This causes time to fall behind by ~170ms per minute of deep sleep
constexpr float kDefaultDriftRateMsPerMin = 170.0f;

struct RTCState
{
  uint32_t magic = 0xDEADBEEF; // Magic number to detect valid RTC data
  uint8_t lastDisplayedMinute = 255;
  bool sensorInitialized = false;
  uint32_t bootCount = 0;
  uint32_t lastNtpSyncBootCount = 0; // Boot count when NTP was last synced
  time_t lastNtpSyncTime = 0;        // Unix timestamp when NTP was last synced
  int32_t lastRtcDriftMs = 0;        // RTC drift in milliseconds (NTP time - RTC time) at last sync
  bool lastRtcDriftValid = false;    // True if lastRtcDriftMs contains valid measurement
  size_t imageSize = 0;              // Size of image data (uncompressed)
  time_t savedTime = 0;              // Saved epoch time before sleep (seconds)
  int32_t savedTimeUs = 0;           // Saved epoch time microseconds part (0-999999)
  uint64_t sleepDurationUs = 0;      // Intended sleep duration in microseconds
  time_t lastUploadedTime = 0;       // Timestamp of the last successfully uploaded data point
  float estimatedProcessingTime = 5.0f; // Estimated boot-to-display time in seconds (adaptive, ms precision)
  float driftRateMsPerMin = kDefaultDriftRateMsPerMin; // Measured RTC drift rate (positive = slow, ms/min)
  bool driftRateCalibrated = false;                    // True after first NTP sync calibrates the rate
  int64_t cumulativeCompensationMs = 0;                // Cumulative drift compensation since last NTP sync (for rate calculation)
};

// Initialize deep sleep manager
void DeepSleepManager_Init();

// Check if this is a wake from deep sleep
bool DeepSleepManager_IsWakeFromSleep();

// Get RTC state (persists across deep sleep)
RTCState &DeepSleepManager_GetRTCState();

// Calculate sleep duration until next minute update (in microseconds)
uint64_t DeepSleepManager_CalculateSleepDuration();

// Enter deep sleep until next minute update
void DeepSleepManager_EnterDeepSleep();

// Get boot count (increments on each wake)
uint32_t DeepSleepManager_GetBootCount();

// Check if WiFi/NTP sync should be performed (on first boot and top of every hour)
// Returns true if WiFi connection and NTP sync are needed
bool DeepSleepManager_ShouldSyncWiFiNtp();

// Save RTC time before NTP sync (call this before attempting NTP sync)
void DeepSleepManager_SaveRtcTimeBeforeSync();

// Save NTP sync duration (call after NTP sync completes, before MarkNtpSynced)
// This compensates for RTC drift calculation - RTC continues running during NTP sync wait
void DeepSleepManager_SaveNtpSyncDuration(unsigned long durationMs);

// Mark NTP as synced and calculate RTC drift (call after successful NTP sync)
void DeepSleepManager_MarkNtpSynced();

// Get RTC drift from last NTP sync (in milliseconds, positive = RTC was behind)
int32_t DeepSleepManager_GetLastRtcDriftMs();

// Check if the last RTC drift measurement is valid
bool DeepSleepManager_IsLastRtcDriftValid();

// Get current drift rate in ms/min (positive = RTC runs slow)
float DeepSleepManager_GetDriftRateMsPerMin();

// Save frame buffer to SD card (or SPIFFS if SD card not available)
// Returns true if successful
bool DeepSleepManager_SaveFrameBuffer(const uint8_t *buffer, size_t size);

// Load frame buffer from SD card (or SPIFFS if SD card not available)
// Returns true if successful
bool DeepSleepManager_LoadFrameBuffer(uint8_t *buffer, size_t size);

// Hold I2C pins high during deep sleep to prevent sensor reset
void DeepSleepManager_HoldI2CPins();

// Release I2C pins hold after wake up
void DeepSleepManager_ReleaseI2CPins();

// Hold EPD pins in safe state during deep sleep to prevent noise
void DeepSleepManager_HoldEPDPins();

// Release EPD pins hold after wake up
void DeepSleepManager_ReleaseEPDPins();

// Check if wakeup was from GPIO (button press)
bool DeepSleepManager_IsWakeFromGPIO();

// Get wakeup GPIO pin number (returns -1 if not GPIO wakeup)
int DeepSleepManager_GetWakeupGPIO();

// Save lastUploadedTime to SD card (persists across power cycles)
// Call this after successful data upload
void DeepSleepManager_SaveLastUploadedTime(time_t timestamp);

// Load lastUploadedTime from SD card
// Returns 0 if file doesn't exist or read fails
time_t DeepSleepManager_LoadLastUploadedTime();
