# Sensor Management Code Review

## 📋 Overview

A document summarizing the management methods and implementation status of the SCD41 CO2/temperature/humidity sensor.

---

## 🏗️ Architecture

### File Structure

```
EPDEnvClock/
├── sensor_manager.h          # Sensor management API (header)
├── sensor_manager.cpp        # Sensor management implementation
└── EPDEnvClock.ino           # Main sketch (initialization/reading calls)
```

### Module Design

- **Sensor Management**: Encapsulated with `SensorManager_*` function group
- **State Management**: Internal state (`sensorInitialized`, `lastTemperature`, etc.) managed within namespace
- **I2C Communication**: Uses SensirionI2cScd4x library

---

## 🔌 Hardware Configuration

### I2C Connection

```cpp
constexpr uint8_t I2C_SDA_PIN = 38;
constexpr uint8_t I2C_SCL_PIN = 21;
constexpr uint8_t SCD4X_I2C_ADDRESS = 0x62;
```

- **SDA**: GPIO 38
- **SCL**: GPIO 21
- **I2C Frequency**: 100kHz (Standard Mode)
- **Address**: 0x62 (default)

---

## 🔄 Initialization Flow

### Cold Boot (`wakeFromSleep=false`)

1. **I2C Bus Initialization**
   - `Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)`
   - `Wire.setClock(100000)` - 100kHz
   - `delay(100)` - Wait for bus stabilization

2. **Sensor Initialization**
   - `scd4x.begin(Wire, SCD4X_I2C_ADDRESS)`

3. **Stop Periodic Measurement Mode**
   - `stopPeriodicMeasurement()` - Stop since periodic measurement runs by default
   - `delay(1000)` - Wait for complete stop

4. **Temperature Offset Setting**
   - `setTemperatureOffset(4.0f)` - Set to 4.0°C
   - Read back setting value for verification (debug purpose)

5. **State Setting**
   - `sensorInitialized = true`
   - Single-shot mode ready

### Wake from Sleep (`wakeFromSleep=true`)

1. **Simplified Initialization**
   - I2C bus initialization only
   - Sensor is already in idle state (periodic measurement stopped)
   - Set `sensorInitialized = true` and complete

**Reason**: The sensor remains powered ON in idle state during Deep Sleep, so re-initialization is not required.

---

## 📊 Measurement Mode

### Current Implementation: **Idle Single-Shot Mode**

**Reasons for Selection**:
- For 1-minute interval measurements, idle single-shot (~1.5mA) is more power-efficient than power-cycled single-shot (~2.6mA)
- ASC (Automatic Self-Calibration) is enabled
- Optimal when measurement interval is less than 380 seconds

**Operation**:
- Sensor always stays in idle state (~0.2mA)
- Only calls `measureSingleShot()` when measuring
- Does not call `powerDown()` before Deep Sleep

### Measurement Flow

```cpp
SensorManager_ReadBlocking(timeoutMs)
  ↓
1. stopPeriodicMeasurement() - For safety (already stopped)
  ↓
2. measureSingleShot() - Start single-shot measurement (waits 5 seconds internally)
  ↓
3. readMeasurement() - Read measurement values
  ↓
4. Update internal state (lastTemperature, lastHumidity, lastCO2)
```

---

## 🔋 Power Saving Strategy

### Current Consumption (1-minute interval measurement)

| Mode | SCD41 Current Consumption | Description |
|------|--------------------------|-------------|
| **Idle Single-Shot** | **~1.5mA** | Current implementation (recommended) |
| Power-Cycled Single-Shot | ~2.6mA | Effective for intervals >380 seconds |
| Low-Power Periodic (30s) | ~3.2mA | Periodic measurement mode |

### Integration with Deep Sleep

- **ESP32-S3**: Tens to hundreds of µA during Deep Sleep
- **SCD41**: Remains in idle state (~0.2mA)
- **Total**: ~0.2-0.3mA during Deep Sleep

**Note**: Do not call `powerDown()` before Deep Sleep (idle single-shot is more power-efficient)

---

## 📖 API Reference

### Initialization

```cpp
bool SensorManager_Begin(bool wakeFromSleep);
```
- Cold boot: Full initialization (stop periodic measurement, set temperature offset)
- Wake from sleep: Simplified initialization (I2C bus initialization only)

### Measurement

```cpp
bool SensorManager_ReadBlocking(unsigned long timeoutMs = 10000);
```
- **Recommended**: For single-shot mode
- Blocking read (waits up to timeoutMs)
- Uses `measureSingleShot()` (waits 5 seconds internally)

```cpp
void SensorManager_Read();
```
- **Deprecated**: For periodic measurement mode
- Non-blocking read
- Checks data readiness with `getDataReadyStatus()`
- **Issue**: May not work since we're currently in single-shot mode

### State Retrieval

```cpp
bool SensorManager_IsInitialized();
float SensorManager_GetTemperature();
float SensorManager_GetHumidity();
uint16_t SensorManager_GetCO2();
```

### Power Control (Currently Unused)

```cpp
void SensorManager_PowerDown();
void SensorManager_WakeUp();
```
- **Note**: Not used for 1-minute interval measurements (idle single-shot is more power-efficient)
- Effective for measurement intervals of 380 seconds or more

---

## 🔍 Usage Locations

### EPDEnvClock.ino

1. **Initialization** (`setup()`)
   ```cpp
   handleSensorInitializationResult(wakeFromSleep);
   sensorInitialized = SensorManager_IsInitialized();
   ```

2. **Initial Reading** (`setup()`)
   ```cpp
   if (sensorInitialized) {
     SensorManager_ReadBlocking(timeoutMs);  // Recommended
     // Fallback: SensorManager_Read();  // Deprecated
   }
   ```

3. **Before Deep Sleep**
   - Do not call `SensorManager_PowerDown()` (maintain idle state)

### display_manager.cpp

1. **Reading on Minute Update**
   ```cpp
   if (SensorManager_IsInitialized()) {
     SensorManager_Read();  // ⚠️ Issue: Does not work in single-shot mode
   }
   ```

2. **Getting Values**
   ```cpp
   float temp = SensorManager_GetTemperature();
   float humidity = SensorManager_GetHumidity();
   uint16_t co2 = SensorManager_GetCO2();
   ```

---

## ⚠️ Issues and Improvement Suggestions

### 1. Usage of `SensorManager_Read()`

**Issue**:
- `SensorManager_Read()` is used at `display_manager.cpp:596`
- This function assumes periodic measurement mode
- Data may not be ready since we're currently in single-shot mode

**Impact**:
- Sensor values may not update on minute update
- Old values may continue to be displayed

**Solution**:
- Use `SensorManager_ReadBlocking()` in `display_manager.cpp`
- Or remove `SensorManager_Read()` and only use values read in `setup()`

### 2. Timeout Setting in `setup()`

**Issue**:
- Timeout is 2 seconds when `wakeFromSleep=true`
- Single-shot mode takes 5 seconds, so timeout is too short

**Current Code**:
```cpp
unsigned long timeoutMs = wakeFromSleep ? 2000 : 5000;
```

**Solution**:
- Single-shot mode always takes 5+ seconds, so unify timeout
- `timeoutMs = 6000;` etc., with some margin

### 3. Fallback Processing

**Issue**:
- If `SensorManager_ReadBlocking()` fails in `setup()`, it calls `SensorManager_Read()` as fallback
- However, `SensorManager_Read()` does not work in single-shot mode

**Solution**:
- Remove fallback processing or output error message and exit

---

## 📈 Expected Current Consumption

### Current Implementation (Idle Single-Shot)

- **SCD41**: ~1.5mA (1-minute interval measurement)
- **ESP32-S3**: ~1mA (mainly Deep Sleep)
- **Total**: ~2.5mA

### Battery Life

- **1480mAh ÷ 2.5mA ≈ 592 hours (~25 days)**

---

## 🔧 Recommended Improvements

### Priority: High

1. **Fix `display_manager.cpp`**
   - Change `SensorManager_Read()` to `SensorManager_ReadBlocking()`
   - Or remove reading on minute update (only use values read in `setup()`)

2. **Fix Timeout Setting**
   - Unify timeout in `setup()` to 6 seconds or more

### Priority: Medium

3. **Review Fallback Processing**
   - Remove fallback to `SensorManager_Read()`

4. **Remove or Warn About `SensorManager_Read()`**
   - Clarify that it cannot be used in single-shot mode

---

## 📝 Summary

### Current Implementation Status

✅ **Good Points**:
- Correctly implemented idle single-shot mode
- Does not call `powerDown()` before Deep Sleep (power saving)
- Appropriate initialization flow

⚠️ **Areas Needing Improvement**:
- Using `SensorManager_Read()` in `display_manager.cpp` (does not work in single-shot mode)
- Inappropriate timeout setting in `setup()`
- Fallback processing does not function

### Recommended Actions

1. Fix `display_manager.cpp` to use `SensorManager_ReadBlocking()`
2. Unify timeout in `setup()` to 6 seconds or more
3. Remove unnecessary fallback processing

---

## 📚 References

- [Sensirion SCD4x Low Power Operation Modes](https://sensirion.com/media/documents/077BC86F/62BF01B9/CD_AN_SCD4x_Low_Power_Operation_D1.pdf)
- [Sensirion SCD4x Datasheet](https://sensirion.com/media/documents/E0F04247/631EF271/CD_DS_SCD40_SCD41_Datasheet_D1.pdf)
