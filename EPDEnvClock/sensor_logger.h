#pragma once

#include <Arduino.h>
#include <time.h>

// Initialize sensor logger (call once in setup)
void SensorLogger_Init();

// Log sensor values to JSONL file on SD card
// Returns true if successful, false otherwise
// rtcDriftMs: RTC drift in milliseconds from last NTP sync (0 if not synced this boot)
// driftValid: true if rtcDriftMs contains a valid measurement
bool SensorLogger_LogValues(
    const struct tm &timeinfo,
    time_t unixTimestamp,
    int32_t rtcDriftMs,
    bool driftValid,
    float temperature,
    float humidity,
    uint16_t co2,
    int batteryRawADC,
    float batteryVoltage);

// Delete log files older than specified days
// Returns number of files deleted
int SensorLogger_DeleteOldFiles(int maxAgeDays = 30);
