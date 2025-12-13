// Copyright (c) 2024
#pragma once

#include <Arduino.h>

// MAX17048 uses separate I2C bus (Wire1)
constexpr uint8_t FUEL_GAUGE_SDA_PIN = 14;
constexpr uint8_t FUEL_GAUGE_SCL_PIN = 16;

// 4054A CHRG pin (open-drain, active LOW when charging)
constexpr uint8_t CHRG_PIN = 8;

// Initialize fuel gauge on Wire1
bool FuelGauge_Init();

// Read battery voltage in volts
// Returns -1.0f if unavailable or reading outside valid range (2.0-4.4V)
float FuelGauge_GetVoltage();

// Read battery state of charge in percent (0-100) from MAX17048
float FuelGauge_GetPercent();

// Calculate linear battery percent based on voltage (3.4V=0%, 4.2V=100%)
// More accurate than MAX17048 below 3.8V based on actual discharge testing
// 3.4V chosen because device crashes with WiFi below this voltage
float FuelGauge_GetLinearPercent(float voltage);

// Read battery charge rate in percent per hour
// Positive = charging, Negative = discharging
float FuelGauge_GetChargeRate();

// Check if fuel gauge is available
bool FuelGauge_IsAvailable();

// Quick start - reset fuel gauge algorithm
void FuelGauge_QuickStart();

// Initialize CHRG pin (call before any I2C operations)
// MUST be called before FuelGauge_Init() to avoid I2C noise interference
void Charging_Init();

// Read charging state from 4054A CHRG pin
// Returns true if charging, false if not charging (or no battery)
// Note: Call this BEFORE I2C operations to get clean reading
bool Charging_IsCharging();
