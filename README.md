# EPDEnvClock

[日本語版はこちら](README_ja.md)

An ESP32-S3 based e-paper clock project with integrated SCD41 CO2/temperature/humidity sensor, designed for low power consumption and long battery life.

## 📋 Overview

EPDEnvClock is a clock application using the CrowPanel ESP32-S3 E-Paper 5.79" display (792x272 pixels). It provides the following features:

- **Time & Date Display**: Shows time and date with large number fonts
- **Environmental Sensor**: Measures and displays CO2, temperature, and humidity using SCD41 sensor
- **Low Power Design**: Long battery life with Deep Sleep mode (updates approximately every minute)
- **Wi-Fi Connectivity**: NTP time synchronization and optional ImageBW data export via Wi-Fi
- **Battery Voltage Monitoring**: Real-time battery voltage display
- **Button Wake-up**: Wake from Deep Sleep with HOME button for full screen refresh

## ✨ Main Features

### Display Features

- **Time Display**: Large number font (Number L) with kerning support
- **Date Display**: Medium-sized number font (Number M) in YYYY.MM.DD format
- **Sensor Values**: Temperature, humidity, and CO2 concentration with icons
- **Status Display**: Battery voltage, Wi-Fi status, NTP sync status, uptime, and free memory

### Sensor Features

- **SCD41 Integration**: Measures CO2 (400-5000ppm), temperature (-10~+60°C), and humidity (0-100%RH)
- **Low Power Mode**: Single-Shot mode with approximately 1.5mA current consumption (waits in Light Sleep)
- **Temperature Compensation**: Temperature offset function to compensate for self-heating (4.0°C)
- **Auto Calibration**: ASC (Automatic Self-Calibration) support

### Power Management

- **Deep Sleep**: Enters Deep Sleep at approximately 1-minute intervals to minimize current consumption
- **Light Sleep**: Uses Light Sleep during 5-second sensor measurement wait to reduce power consumption
- **EPD Deep Sleep**: Display enters Deep Sleep mode to reduce power consumption
- **Frame Buffer Persistence**: Saves frame buffer to SD card or SPIFFS fallback, restores on wake
- **SD Card Power Control**: Powers off SD card during Deep Sleep to reduce current consumption
- **Wi-Fi Power Saving**: NTP sync only every ~60 minutes (60 wake cycles)

### Network Features

- **Wi-Fi Connection**: Automatic Wi-Fi connection
- **NTP Sync**: Syncs time from NTP server every ~60 minutes (maintains RTC time between syncs)
- **ImageBW Export**: Optional Wi-Fi export of display data to server

### Data Logging

- **Sensor Log**: Automatically records sensor values to SD card in JSONL format
- **Recorded Data**: Date, time, Unix timestamp, time since NTP sync, temperature, humidity, CO2, battery ADC value, battery voltage
- **File Format**: `/sensor_logs/sensor_log_YYYYMMDD.jsonl` (files split by date)
- **Fallback**: Falls back to SPIFFS if SD card is unavailable

### Button Functions

- **HOME Button (GPIO 2)**: Wake from Deep Sleep and perform full screen refresh
- **Other Buttons**: EXIT (GPIO 1), PRV (GPIO 6), NEXT (GPIO 4), OK (GPIO 5) - for future expansion
- All buttons are active LOW with internal pullup

## 🔧 Hardware Requirements

### Components Built into CrowPanel

- **ESP32-S3 Dev Module**
- **EPD Display**: 792x272 pixels (controlled by two SSD1683 ICs in master/slave configuration)
- **SD Card Slot**: For frame buffer storage (optional, longer write lifespan than SPIFFS)

### External Components (Optional)

- **SCD41 Sensor**: CO2/temperature/humidity sensor

### Pin Configuration

#### SCD41 Sensor (I2C)

| Pin | GPIO |
|-----|------|
| SDA | 38 |
| SCL | 20 |
| VDD | 3.3V |
| GND | GND |

**Note**: Pull-up resistors are built into the SCD41 module, no additional hardware required.

#### SD Card (HSPI Bus)

| Pin | GPIO |
|-----|------|
| MOSI | 40 |
| MISO | 13 |
| SCK | 39 |
| CS | 10 |
| Power Enable | 42 |

#### EPD Display (Bit-banging SPI)

| Pin | GPIO |
|-----|------|
| MOSI | 11 |
| SCK | 12 |
| CS_M | 45 |
| CS_S | 46 |
| DC | 47 |
| RST | 48 |

#### Buttons (Active LOW)

| Button | GPIO |
|--------|------|
| HOME | 2 |
| EXIT | 1 |
| PRV | 6 |
| NEXT | 4 |
| OK | 5 |

#### Battery ADC

- **GPIO**: 8
- **Calibration**: `Vbat = 0.002334 * adc_raw - 1.353`

## 🚀 Setup

### 1. Required Software

#### USB Serial Driver Installation

CrowPanel ESP32-S3 uses the **CH340** USB serial chip.

**macOS (10.14 Mojave and later)**:
No additional driver required. macOS 10.14+ natively supports CH340.

- Device appears as `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*` when connected
- **Note**: Installing additional drivers may cause issues

**macOS (10.13 and earlier)**:
Driver installation required:

- Homebrew: `brew install --cask wch-ch34x-usb-serial-driver`
- Or see [SparkFun CH340 Driver Guide](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers)

**Windows**:
Usually auto-detected. If not recognized:

- Download from [SparkFun CH340 Driver Guide](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers)

**Linux**:
Built into kernel, no installation needed. If not recognized:

```bash
sudo modprobe ch34x
```

#### arduino-cli Installation

**macOS**:

```bash
brew install arduino-cli
```

**Linux**:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

**Windows**:
Download installer from [official site](https://arduino.github.io/arduino-cli/latest/installation/)

#### ESP32 Board Support Installation

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.7
```

**Note**: Use ESP32 core version `2.0.7`. Newer versions may have compatibility issues.

#### Library Installation

```bash
# Sensirion SCD4x library (Sensirion Core dependency is installed automatically)
arduino-cli lib install "Sensirion I2C SCD4x@0.4.0"
```

### Development Environment Versions

| Component | Version | Notes |
|-----------|---------|-------|
| arduino-cli | Latest recommended | `brew install arduino-cli` (macOS) |
| ESP32 Core | 2.0.7 | `esp32:esp32@2.0.7` |
| Sensirion I2C SCD4x | 0.4.0 | CO2/temperature/humidity sensor library |
| Sensirion Core | 0.6.0 | Dependency (auto-installed) |

#### Check Installed Libraries

```bash
arduino-cli lib list
```

#### Check ESP32 Core Version

```bash
arduino-cli core list
```

### 2. Wi-Fi Configuration

Copy `EPDEnvClock/wifi_config.h.example` to `EPDEnvClock/wifi_config.h` and set your Wi-Fi credentials:

```cpp
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
```

**Note**: `wifi_config.h` is included in `.gitignore` and will not be committed.

### 3. ImageBW Export Configuration (Optional)

Set server IP address and port in `EPDEnvClock/server_config.h`:

```cpp
#define ENABLE_IMAGEBW_EXPORT 1  // 1 to enable, 0 to disable
#define SERVER_IP "192.168.1.100"  // Server IP address
#define SERVER_PORT 8080           // Server port
```

## 📦 Build & Upload

### Recommended Method (Compile + Upload)

```bash
cd /path/to/EPDEnvClock
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDEnvClock
```

**Note**: Replace `/path/to/EPDEnvClock` with your actual project directory path. The port name (`/dev/cu.wchusbserial110`) may vary depending on your environment.

**Important**:
- Always use `compile --upload` together (upload alone doesn't guarantee recompile)
- Check port with `arduino-cli board list` - port name varies

**Configuration Parameters**:

- **FQBN**: `esp32:esp32:esp32s3`
- **PartitionScheme**: `huge_app` (Huge APP: 3MB No OTA/1MB SPIFFS)
- **PSRAM**: `opi` (OPI PSRAM)

### Check Port

```bash
arduino-cli board list
```

## 💻 Usage

### Basic Operation

1. **Startup**: Automatically starts when power is supplied to ESP32-S3
2. **Initialization**: Sensor and Wi-Fi initialization (on first boot)
3. **Display Update**: Display updates approximately every minute
4. **Deep Sleep**: Enters Deep Sleep mode after display update
5. **Button Wake**: Press HOME button to wake from Deep Sleep and perform full screen refresh

### Display Layout

Screen layout (792x272 pixels):

- **Top (y=4)**: Status information (battery voltage, Wi-Fi status, NTP sync status, uptime, free memory)
- **Upper Left (y=45)**: Date (YYYY.MM.DD format, medium-sized numbers)
- **Center Left (y=123)**: Time (H:MM or HH:MM format, large numbers)
- **Upper Right (y=33)**: Temperature (icon + value + °C unit)
- **Center Right (y=114)**: Humidity (icon + value + % unit)
- **Lower Right (y=193)**: CO2 concentration (icon + value + ppm unit)

### ImageBW Export Feature (Optional)

To send display data to a server via Wi-Fi:

1. **Start Server** (Python 3 required):

```bash
cd /path/to/EPDEnvClock
python3 scripts/imagebw_server.py --port 8080
```

2. **Arduino Configuration**: Set server IP address in `server_config.h`

3. **Auto Send**: ImageBW data is automatically sent whenever the display updates

Received data is saved as PNG files in the `output/` directory.

**Note**: The server runs on any platform with Python 3 (Windows, macOS, Linux, etc.).

See [docs/README_IMAGEBW.md](./docs/README_IMAGEBW.md) for details.

## 📁 Project Structure

```
EPDEnvClock/
├── EPDEnvClock/                  # Arduino/Firmware code (sketch directory)
│   ├── EPDEnvClock.ino          # Main sketch (setup/loop)
│   ├── EPD.h / EPD.cpp          # Low-level EPD driver
│   ├── EPD_Init.h / EPD_Init.cpp  # EPD initialization
│   ├── spi.h / spi.cpp          # Bit-banging SPI for EPD
│   ├── display_manager.*        # Display rendering, layout, battery reading
│   ├── font_renderer.*          # Glyph drawing with kerning support
│   ├── sensor_manager.*         # SCD41 sensor (single-shot mode with light sleep)
│   ├── network_manager.*        # Wi-Fi connection, NTP sync
│   ├── deep_sleep_manager.*     # Deep sleep, RTC state, SD/SPIFFS frame buffer
│   ├── imagebw_export.*         # ImageBW Export
│   ├── logger.*                 # Logging with levels (DEBUG/INFO/WARN/ERROR)
│   ├── EPDfont.h                # Font data (12px text)
│   ├── wifi_config.h            # Wi-Fi configuration (gitignore)
│   ├── server_config.h          # Server configuration
│   └── bitmaps/                 # Number fonts (L/M), icons, units, kerning table
├── scripts/                     # Python scripts
│   ├── convert_image.py         # Image conversion script
│   ├── convert_imagebw.py       # ImageBW conversion script
│   ├── convert_numbers.py       # Number image conversion script
│   ├── create_number_bitmaps.py # Number bitmap generation script
│   └── imagebw_server.py        # ImageBW receiver server
├── assets/                      # Assets (image files, etc.)
│   ├── Number L/                # Large number font images
│   ├── Number M/                # Medium number font images (58px height)
│   └── Number S/                # Small number font images
├── web/                         # Web dashboard (Astro + Cloudflare Pages)
├── docs/                        # Documentation
│   ├── README.md                # Documentation index
│   ├── README_IMAGEBW.md        # ImageBW feature guide
│   ├── README_SCD41.md          # SCD41 sensor guide
│   └── reviews/                 # Code reviews
├── output/                      # Generated image output (gitignore)
└── README.md                    # This file
```

## 📚 Documentation

- **[docs/README.md](./docs/README.md)** - Documentation index
- **[docs/README_IMAGEBW.md](./docs/README_IMAGEBW.md)** - ImageBW Wi-Fi Export feature guide
- **[docs/README_SCD41.md](./docs/README_SCD41.md)** - SCD41 sensor integration guide
- **[web/README.md](./web/README.md)** - Web dashboard documentation

## 🌐 Web Dashboard

The project includes a web dashboard for viewing sensor data, deployed on Cloudflare Pages.

### Local Development

```bash
cd web
bun install
bun run dev
```

Access at http://localhost:4321/

### Deployment

```bash
cd web
bun run build
bunx wrangler pages deploy dist --branch=main
```

**Note**: `--branch=main` is required to deploy to production domain. Without it, deploys to preview URL only.

## 🔋 Low Power Design

### Current Consumption

| State | Current Consumption |
|-------|---------------------|
| SCD41 Idle Single-Shot | ~1.5mA |
| ESP32-S3 Deep Sleep | ~0.2-0.3mA |
| ESP32-S3 Light Sleep (sensor measurement wait) | ~2-3mA |
| ESP32-S3 Active (including Wi-Fi) | ~80-150mA |

### Battery Life (with 1480mAh battery)

- **Average Current Consumption**: ~2.5mA (Wi-Fi sync once per hour)
- **Battery Life**: 1480mAh ÷ 2.5mA ≈ **592 hours (~25 days)**

### Deep Sleep Cycle

- **Update Interval**: ~1 minute (updates at minute boundary)
- **Active Time**: ~6-8 seconds (5s sensor measurement + display update + initialization)
- **Deep Sleep Time**: ~52-54 seconds
- **Wi-Fi Connection**: Once every 60 wake cycles (~once per hour)

### Power Optimization

1. **Light Sleep During Sensor Measurement**: Uses Light Sleep during 5-second Single-Shot measurement wait
2. **Minimized Wi-Fi Connection**: NTP sync once per hour, uses RTC time otherwise
3. **SD Card Power Control**: Powers off SD card during Deep Sleep (GPIO 42 LOW)
4. **EPD Deep Sleep**: Transitions display to Deep Sleep mode
5. **I2C Pins Held HIGH**: Keeps sensor in idle mode during deep sleep

## 🎨 Font Generation

Number fonts (Number S, Number M, Number L) are generated using `scripts/create_number_bitmaps.py`.

### Font Used

**Important**: All number fonts use the following font file:

- **Font Name**: Baloo Bhai 2
- **Style**: Extra Bold

### Number M Font Generation Example

```bash
cd /path/to/EPDEnvClock
python3 scripts/create_number_bitmaps.py \
  --font-path "/path/to/fonts/BalooBhai2-ExtraBold.ttf" \
  --font-size-px 90 \
  --output-dir "assets/Number M"
```

## ⚙️ Technical Specifications

### ESP32-S3 Configuration

- **Board**: ESP32S3 Dev Module
- **Partition Scheme**: Huge APP (3MB No OTA/1MB SPIFFS)
- **PSRAM**: OPI PSRAM
- **CPU Frequency**: 240MHz (Wi-Fi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 4MB (32Mb)
- **Upload Speed**: 921600

### EPD Display Specifications

- **Actual Resolution**: 792x272 pixels
- **Controller**: Two SSD1683 ICs in master/slave configuration
  - Each controller: Handles 396x272 pixels
  - 4px gap in center (connection between controllers)
- **Program Definition**: `EPD_W = 800`, `EPD_H = 272` (for address offset)
- **Buffer Size**: 800x272 = 27,200 bytes
- **Interface**: Bit-banging SPI (pins 11, 12, 45, 46, 47, 48)

### SCD41 Sensor Specifications

- **I2C Address**: 0x62 (default)
- **I2C Pins**: SDA=GPIO 38, SCL=GPIO 20
- **Measurement Mode**: Single-Shot (waits 5 seconds in Light Sleep)
- **Temperature Offset**: 4.0°C (self-heating compensation)
- **Measurement Range**:
  - CO2: 400-5000ppm
  - Temperature: -10~+60°C
  - Humidity: 0-100%RH
- **Accuracy**:
  - CO2: ±(40ppm+5%)
  - Temperature: ±0.8°C (in 15-35°C range)
  - Humidity: ±6%RH (in 15-35°C, 20-65%RH range)

### Time Management

- **NTP Server**: `ntp.nict.jp`
- **Timezone**: JST (UTC+9)
- **Sync Interval**: Every 60 wake cycles (~1 hour)
- **RTC Persistence**: Time saved to RTC memory before sleep, restored on wake

### Logger Feature

- **Log Levels**: DEBUG, INFO, WARN, ERROR
- **Timestamp**: Boot time, date/time, or both
- **Tags**: Setup, Loop, Network, Sensor, Display, Font, DeepSleep, ImageBW
- **ANSI Colors**: Color-coded display by log level

## ⚠️ Common Pitfalls

1. **SCL pin is GPIO 20**, not 21
2. **Date format uses periods**: YYYY.MM.DD (not slashes)
3. **Frame buffer is 27,200 bytes** (800x272, not 792x272)
4. **EPD uses bit-banging SPI** (pins 11,12,45,46,47,48), SD uses hardware HSPI
5. **Button pins are active LOW** with internal pullup
6. **SD card needs power enable** (GPIO 42 HIGH) before use
7. **Sketch directory name must match .ino filename** (`EPDEnvClock/EPDEnvClock.ino`)

## 🐛 Troubleshooting

### Compile Errors

- **Error**: "Invalid FQBN"
  - **Solution**: Check FQBN format. Options are separated by `:` (e.g., `esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi`)

- **Error**: "SensirionI2cScd4x.h: No such file or directory"
  - **Solution**: Correct header name is `SensirionI2CScd4x.h` (note case sensitivity)
  - Install library: `arduino-cli lib install "Sensirion I2C SCD4x@0.4.0"`

- **Error**: "no matching function for call to 'SensirionI2CScd4x::begin'"
  - **Solution**: Library version 0.4.0 uses `scd4x.begin(Wire)` (no I2C address argument)

- **Error**: "'class SensirionI2CScd4x' has no member named 'getDataReadyStatus'"
  - **Solution**: Changed to `getDataReadyFlag()` in version 0.4.0

### Upload Errors

- **Error**: "Unable to verify flash chip connection"
  - **Solution**:
    - Try a different USB port (check with `arduino-cli board list`)
    - Press the board's reset button
    - Check USB cable (ensure it supports data transfer)

- **Error**: Port not found
  - **Solution**:
    - Reconnect USB cable
    - Recheck port with `arduino-cli board list`
    - Use a data-capable USB cable (charging-only cables won't work)
    - Install CH340 driver for macOS 10.13 or earlier (see "USB Serial Driver Installation" above)
    - Try a different USB port

### Sensor Won't Initialize

1. **Check Connections**:
   - Is SDA connected to GPIO 38?
   - Is SCL connected to GPIO 20?
   - Is VDD connected to 3.3V?
   - Is GND connected?

2. **Check I2C Bus**:
   - Use I2C scanner to verify sensor is detected
   - Default I2C address: 0x62

3. **Check Power**:
   - Verify SCD41 supply voltage is 3.3V±0.1V
   - Check if sensor was reset after Deep Sleep

See the "Troubleshooting" section in [docs/README_SCD41.md](./docs/README_SCD41.md) for details.

### Time is Incorrect

1. **Check Wi-Fi Connection**: NTP sync requires Wi-Fi connection
2. **Check RTC Time**: Time is restored from RTC after Deep Sleep
3. **Timezone**: JST (UTC+9) is configured

### SD Card Not Recognized

1. **SD Card Format**: Format as FAT32
2. **Power Pin**: Check if GPIO 42 is HIGH
3. **SPI Pins**: MOSI=40, MISO=13, SCK=39, CS=10

**Note**: If SD card is unavailable, falls back to SPIFFS, but write lifespan is limited.

## 📝 License

License information for this project is not specified. Please check the license of each library when using.

## 📧 Contact

If you have questions or issues with the project, please report them on GitHub Issues.
