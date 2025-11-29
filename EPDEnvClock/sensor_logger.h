#pragma once

#include <Arduino.h>
#include <time.h>

// Initialize sensor logger (call once in setup)
void SensorLogger_Init();

// Log sensor values to JSONL file on SD card
// Returns true if successful, false otherwise
// rtcDriftMs: RTC drift in milliseconds from last NTP sync (0 if not synced this boot)
// driftValid: true if rtcDriftMs contains a valid measurement
// batteryChargeRate: battery charge/discharge rate in %/hr (positive=charging, negative=discharging)
bool SensorLogger_LogValues(
    const struct tm &timeinfo,
    time_t unixTimestamp,
    int32_t rtcDriftMs,
    bool driftValid,
    float temperature,
    float humidity,
    uint16_t co2,
    float batteryVoltage,
    float batteryPercent,
    float batteryChargeRate);

// Delete log files older than specified days
// Returns number of files deleted
int SensorLogger_DeleteOldFiles(int maxAgeDays = 30);

// Get unsent sensor readings from log files
// lastUploadedTime: timestamp of the last successfully uploaded data point
// payload: String to append the JSON array of readings to
// latestTimestamp: Output parameter to store the timestamp of the last reading added
// maxReadings: maximum number of readings to retrieve
// Returns: number of readings added to payload
int SensorLogger_GetUnsentReadings(time_t lastUploadedTime, String &payload, time_t &latestTimestamp, int maxReadings = 60);
