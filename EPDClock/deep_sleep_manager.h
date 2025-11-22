#pragma once

#include <Arduino.h>
#include <sys/time.h>

// RTC memory structure to persist across deep sleep
struct RTCState
{
  uint32_t magic = 0xDEADBEEF; // Magic number to detect valid RTC data
  uint8_t lastDisplayedMinute = 255;
  bool sensorInitialized = false;
  uint32_t bootCount = 0;
  uint32_t lastNtpSyncBootCount = 0; // Boot count when NTP was last synced
  size_t imageSize = 0;              // Size of image data (uncompressed)
  time_t savedTime = 0;              // Saved epoch time before sleep
  uint64_t sleepDurationUs = 0;      // Intended sleep duration
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

// Check if NTP resync is needed (based on boot count interval)
// Returns true if last sync was more than intervalBoots ago
bool DeepSleepManager_ShouldResyncNtp(uint32_t intervalBoots = 60); // Default: every 60 boots (~60 minutes)

// Check if WiFi/NTP sync should be performed (1 hour interval)
// Returns true if WiFi connection and NTP sync are needed
bool DeepSleepManager_ShouldSyncWiFiNtp();

// Mark NTP as synced (updates lastNtpSyncBootCount)
void DeepSleepManager_MarkNtpSynced();

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
