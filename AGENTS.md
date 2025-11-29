# AGENTS.md - AI Agent Guidelines for EPDEnvClock

## Project Overview

ESP32-S3 based e-paper clock with SCD41 CO2/temperature/humidity sensor. Uses CrowPanel 5.79" E-Paper display (792x272 pixels).

## Build & Upload

```bash
cd /path/to/EPDEnvClock
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDEnvClock
```

- Always use `compile --upload` together (upload alone doesn't guarantee recompile)
- Check port with `arduino-cli board list` - port name varies
- Required libraries:
  - `arduino-cli lib install "Sensirion I2C SCD4x"`
  - `arduino-cli lib install "Adafruit MAX1704X"`

## Critical Hardware Details

### EPD Display

- **Actual resolution**: 792x272 pixels
- **Buffer size**: 800x272 = 27,200 bytes (EPD_W=800 for address offset)
- **Controller**: Dual SSD1683 ICs (master/slave, 396px each, 4px gap in center)

### SCD41 Sensor I2C Pins (Wire - Bus 0)

- **SDA**: GPIO 38
- **SCL**: GPIO 20
- **I2C Address**: 0x62

### MAX17048 Fuel Gauge I2C Pins (Wire1 - Bus 1)

- **SDA**: GPIO 14
- **SCL**: GPIO 16
- **I2C Address**: 0x36
- **Note**: Uses separate I2C bus from SCD41 sensor
- **Power**: Powered by battery (CELL+/CELL- must be connected to LiPo)

### SD Card SPI Pins (HSPI bus)

- MOSI=40, MISO=13, SCK=39, CS=10, Power=42

### Button Pins (active LOW)

- HOME=2, EXIT=1, PRV=6, NEXT=4, OK=5

### Battery Monitoring (MAX17048)

- **I2C Bus**: Wire1 (SDA=14, SCL=16)
- **I2C Address**: 0x36
- **Reported**: Voltage (V), State of Charge (%), Charge Rate (%/hr)
- **Power**: Chip is powered by battery (must connect CELL+/CELL- to LiPo)

## Code Architecture

```
EPDEnvClock/
├── EPDEnvClock.ino      # Main sketch (setup/loop)
├── display_manager.*    # Display rendering, layout, battery reading
├── sensor_manager.*     # SCD41 sensor (single-shot mode with light sleep)
├── fuel_gauge_manager.* # MAX17048 fuel gauge (battery %, voltage, charge rate)
├── network_manager.*    # Wi-Fi connection, NTP sync
├── deep_sleep_manager.* # Deep sleep, RTC state, SD/SPIFFS frame buffer
├── font_renderer.*      # Glyph drawing with kerning support
├── logger.*             # Logging with levels (DEBUG/INFO/WARN/ERROR)
├── EPD.*, EPD_Init.*    # Low-level EPD driver
├── spi.*                # Bit-banging SPI for EPD
└── bitmaps/             # Number fonts (L/M), icons, units, kerning table
```

## Key Implementation Details

### Power Management

- Deep sleep ~52-54 seconds, wake at minute boundary
- Light sleep during 5-second sensor measurement
- Wi-Fi/NTP sync only every 60 boots (~1 hour)
- SD card power off during deep sleep (GPIO 42 LOW)
- I2C pins held HIGH during sleep to keep sensor in idle mode

### Display Update Flow

1. Check if minute changed (skip update if same)
2. Clear buffer, draw time/date/sensor values
3. `EPD_Display()` → `EPD_PartUpdate()` (or `EPD_Update()` for full refresh)
4. Save frame buffer to SD (or SPIFFS fallback)
5. `EPD_DeepSleep()` before entering deep sleep

### Time Management

- NTP server: `ntp.nict.jp`, Timezone: JST (UTC+9)
- Time saved to RTC memory before sleep, restored on wake
- `kNtpSyncIntervalBoots = 60` (sync every 60 wakes)

### Sensor Reading

- Single-shot mode: send 0x219d command, light sleep 5s, read result
- Temperature offset: 4.0°C (compensates for self-heating)
- Falls back to periodic mode if single-shot fails

## Conventions

- All commit messages in English
- Sketch directory name must match .ino filename (`EPDEnvClock/EPDEnvClock.ino`)
- `wifi_config.h` is gitignored - use `wifi_config.h.example` as template
- Number fonts use "Baloo Bhai 2 Extra Bold" font

## Common Pitfalls

1. **SCL pin is GPIO 20**, not 21
2. **Date format uses periods**: YYYY.MM.DD (not slashes)
3. **Frame buffer is 27,200 bytes** (800x272, not 792x272)
4. **EPD uses bit-banging SPI** (pins 11,12,45,46,47,48), SD uses hardware HSPI
5. **Button pins are active LOW** with internal pullup
6. **SD card needs power enable** (GPIO 42 HIGH) before use

## ImageBW Export Server (Optional)

```bash
python3 scripts/imagebw_server.py --port 8080
```

Enable in `server_config.h`: `#define ENABLE_IMAGEBW_EXPORT 1`

## Font Bitmap Generation

```bash
python3 scripts/create_number_bitmaps.py \
  --font-path "/path/to/BalooBhai2-ExtraBold.ttf" \
  --font-size-px 90 \
  --output-dir "assets/Number M"
```

## Web Dashboard Deployment

```bash
cd web
bun run build
bunx wrangler pages deploy dist --branch=main
```

- `--branch=main` is required to deploy to production domain
- Without it, deploys to preview URL only
- Local dev server: `bun run dev` → http://localhost:4321/
