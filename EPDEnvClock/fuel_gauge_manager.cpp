// Copyright (c) 2024
#include "fuel_gauge_manager.h"

#include <Adafruit_MAX1704X.h>
#include <Wire.h>

#include "logger.h"

namespace {
Adafruit_MAX17048 maxlipo;
bool fuelGaugeAvailable = false;
bool wireInitialized = false;
bool chrgPinInitialized = false;
bool lastChargingState = false;
}  // namespace

// ============================================================
// CHRG Pin Functions (4054A charging status)
// ============================================================
// CHRG is open-drain: LOW = charging, HIGH-Z = not charging
// Must be read BEFORE I2C operations to avoid noise interference

void Charging_Init() {
  if (chrgPinInitialized) {
    return;
  }
  // Configure as input with internal pullup
  // CRITICAL: Never set this pin as OUTPUT (could damage 4054A)
  pinMode(CHRG_PIN, INPUT_PULLUP);
  chrgPinInitialized = true;

  // Read initial state
  lastChargingState = (digitalRead(CHRG_PIN) == LOW);
  LOGI(LogTag::SENSOR, "CHRG pin initialized on GPIO %d, state: %s",
       CHRG_PIN, lastChargingState ? "CHARGING" : "NOT CHARGING");
}

bool Charging_IsCharging() {
  if (!chrgPinInitialized) {
    Charging_Init();
  }

  // Simple read - debouncing not needed here since we read before I2C
  bool charging = (digitalRead(CHRG_PIN) == LOW);

  // Log state changes
  if (charging != lastChargingState) {
    LOGI(LogTag::SENSOR, "Charging state changed: %s -> %s",
         lastChargingState ? "CHARGING" : "NOT CHARGING",
         charging ? "CHARGING" : "NOT CHARGING");
    lastChargingState = charging;
  }

  return charging;
}

// Recover I2C bus by bit-banging SCL to release a stuck SDA line.
// When a slave holds SDA LOW (e.g. mid-transfer interrupted by deep sleep),
// toggling SCL up to 9 times clocks out the stuck byte/ACK, then a STOP
// condition (SDA LOW→HIGH while SCL is HIGH) resets the bus state.
void recoverI2CBus(uint8_t sdaPin, uint8_t sclPin) {
  LOGI(LogTag::SENSOR, "I2C bus recovery on SDA:%d SCL:%d", sdaPin, sclPin);

  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, OUTPUT);

  // Clock up to 9 times to let the slave release SDA
  for (int i = 0; i < 9; i++) {
    if (digitalRead(sdaPin) == HIGH) {
      LOGD(LogTag::SENSOR, "I2C bus recovered after %d clock pulses", i);
      break;
    }
    digitalWrite(sclPin, LOW);
    delayMicroseconds(5);
    digitalWrite(sclPin, HIGH);
    delayMicroseconds(5);
  }

  // Generate STOP condition: SDA LOW→HIGH while SCL is HIGH
  pinMode(sdaPin, OUTPUT);
  digitalWrite(sdaPin, LOW);
  delayMicroseconds(5);
  digitalWrite(sclPin, HIGH);
  delayMicroseconds(5);
  digitalWrite(sdaPin, HIGH);
  delayMicroseconds(5);

  // Release pins back to input for Wire1 to take over
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
}

bool FuelGauge_Init() {
  // MAX17048 uses separate I2C bus (Wire1) on GPIO 14/16
  // Note: Must be called AFTER DeepSleepManager_ReleaseI2CPins() to ensure
  // gpio_hold is released before Wire1 takes control of the pins.
  constexpr int kMaxAttempts = 3;

  for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
    if (!wireInitialized) {
      // On retry, perform I2C bus recovery before re-initializing Wire1.
      // This handles cases where SDA is stuck LOW (slave hung mid-transfer).
      if (attempt > 1) {
        recoverI2CBus(FUEL_GAUGE_SDA_PIN, FUEL_GAUGE_SCL_PIN);
        delay(50);
      }

      if (!Wire1.begin(FUEL_GAUGE_SDA_PIN, FUEL_GAUGE_SCL_PIN)) {
        LOGE(LogTag::SENSOR, "Wire1.begin() failed (SDA:%d, SCL:%d, attempt %d/%d)",
             FUEL_GAUGE_SDA_PIN, FUEL_GAUGE_SCL_PIN, attempt, kMaxAttempts);
        continue;
      }
      Wire1.setClock(100000);
      wireInitialized = true;
      delay(50);
    }

    if (maxlipo.begin(&Wire1)) {
      if (attempt > 1) {
        LOGI(LogTag::SENSOR, "MAX17048 found on attempt %d", attempt);
      }
      LOGI(LogTag::SENSOR, "MAX17048 found on Wire1 (SDA:%d, SCL:%d)",
           FUEL_GAUGE_SDA_PIN, FUEL_GAUGE_SCL_PIN);

      // Quick Start to ensure SOC is calculated
      // MAX17048 needs this after power-up or if SOC is stuck
      // Takes ~175ms to complete per datasheet
      maxlipo.quickStart();
      delay(250);  // Wait for Quick Start to complete

      fuelGaugeAvailable = true;
      return true;
    }

    LOGW(LogTag::SENSOR, "MAX17048 not found (attempt %d/%d, SDA:%d, SCL:%d)",
         attempt, kMaxAttempts, FUEL_GAUGE_SDA_PIN, FUEL_GAUGE_SCL_PIN);

    // Tear down Wire1 and retry with fresh I2C bus init
    Wire1.end();
    wireInitialized = false;
    delay(100);
  }

  fuelGaugeAvailable = false;
  return false;
}

float FuelGauge_GetVoltage() {
  if (!fuelGaugeAvailable) {
    return -1.0f;  // Error: not available
  }
  float voltage = maxlipo.cellVoltage();

  // Validate voltage range (2.0V - 4.4V)
  // Values outside this range indicate sensor error or malfunction
  constexpr float kMinValidVoltage = 2.0f;
  constexpr float kMaxValidVoltage = 4.4f;

  if (voltage < kMinValidVoltage || voltage > kMaxValidVoltage) {
    LOGW(LogTag::SENSOR, "MAX17048 voltage out of range: %.3fV (valid: %.1f-%.1fV)",
         voltage, kMinValidVoltage, kMaxValidVoltage);
    return -1.0f;  // Error: invalid reading
  }

  return voltage;
}

float FuelGauge_GetPercent() {
  if (!fuelGaugeAvailable) {
    return -1.0f;
  }
  float percent = maxlipo.cellPercent();
  // Clamp to 0-100 range
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  return percent;
}

float FuelGauge_GetLinearPercent(float voltage) {
  // Linear interpolation: 3.4V = 0%, 4.2V = 100%
  // Based on actual discharge testing (Dec 2025):
  // - Device crashes at ~3.4V with WiFi due to brownout
  // - More accurate than MAX17048's ModelGauge below 3.8V
  // - 0% means "charge now" with small safety margin
  constexpr float kEmptyVoltage = 3.4f;
  constexpr float kFullVoltage = 4.2f;

  float percent = (voltage - kEmptyVoltage) / (kFullVoltage - kEmptyVoltage) * 100.0f;

  // Clamp to 0-100 range
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;

  return percent;
}

float FuelGauge_GetChargeRate() {
  if (!fuelGaugeAvailable) {
    return -1.0f;
  }
  return maxlipo.chargeRate();
}

bool FuelGauge_IsAvailable() {
  return fuelGaugeAvailable;
}

void FuelGauge_QuickStart() {
  if (!fuelGaugeAvailable) {
    return;
  }
  maxlipo.quickStart();
  LOGI(LogTag::SENSOR, "MAX17048 quick start triggered");
}
