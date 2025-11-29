# SCD41 Sensor Integration Guide

This document explains how to connect the SCD41 CO2/temperature/humidity sensor to the CrowPanel ESP32-S3.

## Hardware Connection

### Pin Connections

- **SCD41 VDD** → ESP32-S3 **3.3V**
- **SCD41 GND** → ESP32-S3 **GND**
- **SCD41 SDA** → ESP32-S3 **GPIO 38**
- **SCD41 SCL** → ESP32-S3 **GPIO 20**

**Note**: Pull-up resistors are built into the SCD41 module, so no additional hardware is required.

## Library Installation

### Sensirion SCD4x Arduino Library

The Sensirion SCD4x Arduino library is required to use the SCD41 sensor.

#### Using arduino-cli

```bash
arduino-cli lib install "Sensirion I2C SCD4x"
```

Or install directly from the library's GitHub repository:

```bash
arduino-cli lib install --git-url https://github.com/Sensirion/arduino-i2c-scd4x.git
```

#### Using Arduino IDE

1. Open Arduino IDE
2. **Sketch** → **Include Library** → **Manage Libraries...**
3. Type "Sensirion I2C SCD4x" in the search bar
4. Select "Sensirion I2C SCD4x" and install

### Verify Library Installation

Check installed libraries:

```bash
arduino-cli lib list | grep -i scd4x
```

## Software Configuration

### I2C Pin Settings

I2C pins are configured in the code as follows:

```cpp
#define I2C_SDA_PIN 38
#define I2C_SCL_PIN 20
```

### Sensor Reading Interval

By default, sensor values are read every 5 seconds:

```cpp
#define SENSOR_READ_INTERVAL 5000  // milliseconds
```

### Temperature Offset Setting

The SCD41 sensor tends to measure temperatures higher than the actual ambient temperature due to self-heating. To compensate for this issue, a temperature offset is set during sensor initialization.

**Explanation from Official Documentation**:

According to Sensirion's official datasheet ([SCD4x Datasheet](https://admin.sensirion.com/media/documents/48C4B7FB/64C134E7/Sensirion_SCD4x_Datasheet.pdf)):

> "The RH and T output signal of the SCD4x can be leveraged by correctly setting the temperature offset inside the SCD4x. The temperature offset can depend on various factors such as the SCD4x measurement mode, self-heating of close components, ambient temperature, air flow etc. Thus, the SCD4x temperature offset should be determined after integration into the final device and after thermal equilibration under normal operating conditions (including the operating mode used in the application)."

**Definition and Sign of Temperature Offset**:

According to the datasheet, the temperature offset is defined as:

- **Temperature Offset = Measured Temperature - Actual Ambient Temperature**

This means if the sensor measures a **higher temperature than the actual ambient temperature**, the offset is a **positive value**.

**Sensor's Internal Default Value**:

- **4.0°C is stored by default in the sensor's EEPROM** (factory setting)
- This value is stored inside the sensor and persists even when power is turned off
- During initialization, the current offset value is read and displayed

**Important Behavior**:

- `setTemperatureOffset()` **overwrites (replaces)** the existing value
- **How temperature offset is applied**: `Displayed Temperature = Actual Ambient Temperature - Offset`
- **Calculation example**:
  - If the sensor displays 28°C with the default +4°C offset:
    - Actual ambient temperature = 28 - 4 = 24°C
  - If you set an offset of -3°C:
    - Displayed temperature = 24 - 3 = 21°C (if offset is negative, it subtracts further from actual ambient temperature)
    - **Result**: 28°C → 21°C (**7°C decrease**)
  - In other words, changing from +4°C to -3°C will lower the displayed temperature by 7°C

**Settings in This Project**:

- Setting in this project: **4.0°C** (using the sensor's default value)
- During initialization, the current offset value inside the sensor is read, then the value is set
- After setting, it reads again to confirm the value was set correctly

**Note**: The datasheet's "4°C" is defined as a correction value for when the sensor measures 4°C higher than the actual ambient temperature. However, depending on the implementation of the `setTemperatureOffset()` method, the sign may differ. Please verify the actual behavior and set an appropriate value.

**Recommended Offset Range**:

- Official recommended range: **0°C to 20°C** (positive values)
- Adjustment may be needed depending on actual usage environment

This offset is stored inside the sensor and applied to the read temperature (either added or subtracted depending on implementation).

**Adjusting the Offset Value**:

You can adjust the offset value by modifying the following constant in `EPDEnvClock/sensor_manager.cpp`:

```cpp
scd4x.setTemperatureOffset(4.0f);  // Default: 4.0°C
```

**Recommended Adjustment Method** (based on official documentation):

1. Make adjustments **after integration into the final device** (not the sensor alone, but in the actual usage environment)
2. Measure under **normal operating conditions** (including the operating mode used in the application)
3. Measure the temperature difference **after thermal equilibrium is reached** (allow sufficient time after sensor startup)
4. Measure the actual temperature difference by comparing with an accurate thermometer
5. Adjust the offset value based on the measured temperature difference (official recommended range: 0°C to 20°C)
6. Restart the sensor and confirm the set offset is applied

**Notes**:

- Once set, the offset is stored inside the sensor and persists even when power is turned off
- The sensor must be restarted when setting a new offset value
- Temperature offset **also affects relative humidity (RH) accuracy**, so accurate setting is important

## Usage

### Normal Operation

The sensor values are automatically read and displayed on the e-paper display along with the current time and date. The system operates as follows:

1. On wake from deep sleep, the sensor is initialized
2. A single-shot measurement is triggered (takes ~5 seconds)
3. CO2 (ppm), temperature (°C), and humidity (%RH) are displayed on the EPD
4. The system enters deep sleep until the next minute

### Debugging via Serial

Connect to the serial monitor at 115200 bps to see sensor initialization status and any error messages.

## Troubleshooting

### Sensor Won't Initialize

1. **Check connections**:
   - Is SDA connected to GPIO 38?
   - Is SCL connected to GPIO 20?
   - Is VDD connected to 3.3V?
   - Is GND connected?

2. **Verify I2C bus**:
   - Use an I2C scanner to confirm the sensor is detected
   - Default I2C address: 0x62

3. **Check power**:
   - SCD41 operates at 2.4V-5.5V, but 3.3V is recommended
   - Verify power is stable

### Cannot Read Data

1. **Initialization time**: The sensor requires about 5 seconds for initial startup
2. **Reading interval**: Periodic reading intervals of 5 seconds or more are recommended (considering 60-second sensor response time)
3. **Serial output**: Check error messages to identify the problem

### Temperature Reads High

The SCD41 sensor tends to measure temperatures higher than the actual ambient temperature due to self-heating. The following measures are implemented to address this issue:

1. **Temperature offset setting**: A 4.0°C offset is set during sensor initialization (sensor's default value)
2. **Offset adjustment**: Compare with an accurate thermometer and adjust the offset value as needed

**Verifying Temperature Offset**:

If the following messages appear in serial output, the offset setting was successful:

```text
Temperature offset set to 4.0°C successfully.
Read back temperature offset: 4.00 °C
```

**Notes**:

- The sensor's EEPROM stores a default 4.0°C offset
- This project uses this default value as-is
- The temperature offset is for compensating self-heating

**If Offset Setting Fails**:

A warning message will be displayed, but sensor operation is not affected. Offset setting is an optional feature.

## Sensor Specifications

- **I2C Address**: 0x62 (default)
- **Measurement Range**:
  - CO2: 400-5000 ppm
  - Temperature: -10 to +60°C
  - Humidity: 0-100% RH
- **Accuracy**:
  - CO2: ±(40 ppm + 5%)
  - Temperature: ±0.8°C (in the 15-35°C range)
  - Humidity: ±6% RH (in the 15-35°C, 20-65% RH range)

## Reference Resources

### Official Documentation

- **[Sensirion SCD4x Datasheet (Official Datasheet)](https://admin.sensirion.com/media/documents/48C4B7FB/64C134E7/Sensirion_SCD4x_Datasheet.pdf)**
  - Contains detailed explanation of temperature offset settings
  - Section: Temperature Offset Compensation

- **[Sensirion SCD4x Python API Documentation](https://sensirion.github.io/python-i2c-scd/api.html)**
  - API reference for temperature offset settings

### Community Resources

- [LaskaKit SCD41 GitHub](https://github.com/LaskaKit/SCD41-CO2-Sensor)
- [Sensirion SCD4x Arduino Library](https://github.com/Sensirion/arduino-i2c-scd4x)
