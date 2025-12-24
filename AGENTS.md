# AGENTS.md - AI Agent Guidelines for EPDEnvClock

## Project Overview

ESP32-S3 based e-paper clock with SCD41 CO2/temperature/humidity sensor. Uses CrowPanel 5.79" E-Paper display (792x272 pixels).

## Build & Upload

This project requires ESP32 Arduino Core 2.0.17 (not 3.x). A project-specific Arduino environment is used.

### First-time Setup

```bash
cd /path/to/EPDEnvClock

# Create project-specific Arduino config (if not exists)
cat > arduino-cli.yaml << 'EOF'
board_manager:
  additional_urls:
    - https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
directories:
  data: /Users/hiko/Desktop/EPDEnvClock/.arduino15
  downloads: /Users/hiko/Desktop/EPDEnvClock/.arduino15/staging
  user: /Users/hiko/Documents/Arduino
EOF

# Install ESP32 core 2.0.17
arduino-cli --config-file arduino-cli.yaml core update-index
arduino-cli --config-file arduino-cli.yaml core install esp32:esp32@2.0.17
```

### Compile & Upload

Use `arduwrap` wrapper for compile and upload (requires `arduwrap serve` running in another terminal):

```bash
scripts/arduwrap compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDEnvClock
```

- Uses project-specific config from `arduino-cli.yaml` automatically
- `--upload` flag is added automatically
- Port is managed by the running server (no `-p` needed)
- Serial monitoring is paused during upload, then reconnects automatically

### Required Libraries

Install in user library dir (shared with system):

```bash
arduino-cli lib install "Sensirion I2C SCD4x"
arduino-cli lib install "Adafruit MAX1704X"
```

## Critical Hardware Details

### EPD Display

- **Actual resolution**: 792x272 pixels
- **Buffer size**: 800x272 = 27,200 bytes (EPD_W=800 for address offset)
- **Controller**: Dual SSD1683 ICs (master/slave, 396px each, 8px address offset in center)

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

### Battery Percentage Calculation

Two percentage values are tracked:

1. **Linear Percent** (`g_batteryPercent`) - Used for display
   - Formula: `(voltage - 3.4V) / (4.2V - 3.4V) * 100%`
   - 3.4V = 0%, 4.2V = 100%
   - More accurate than MAX17048 below 3.8V (based on Dec 2025 discharge testing)
   - Device crashes at ~3.4V with WiFi due to brownout

2. **MAX17048 Percent** (`g_batteryMax17048Percent`) - For reference/logging
   - ModelGauge algorithm from MAX17048
   - Increasingly pessimistic below 3.8V
   - Logged as `batt_max17048_percent` in sensor logs

### Charging Detection (4054A CHRG Pin)

- **GPIO**: 8
- **Mode**: INPUT_PULLUP (open-drain output from 4054A)
- **Logic**: LOW = Charging, HIGH = Not charging (or no battery)
- **Note**: Read BEFORE I2C operations to avoid noise interference

## Code Architecture

```
EPDEnvClock/
├── EPDEnvClock.ino      # Main sketch (setup/loop)
├── parallel_tasks.*     # Dual-core parallel WiFi/NTP + sensor reading
├── display_manager.*    # Display rendering, layout, battery reading
├── sensor_manager.*     # SCD41 sensor (single-shot mode with light sleep)
├── fuel_gauge_manager.* # MAX17048 fuel gauge + 4054A charging detection
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
- Wi-Fi/NTP sync at the top of every hour
- SD card power off during deep sleep (GPIO 42 LOW)
- I2C pins held HIGH during sleep to keep sensor in idle mode

### Dual-Core Parallel Processing

WiFi/NTP sync and sensor reading run in parallel using FreeRTOS tasks:

- **Core 0**: WiFi/NTP task (WiFi stack runs on Core 0)
- **Core 1**: Sensor task (I2C sensor reading)

This reduces startup time by ~2 seconds and enables single screen update (instead of two-phase update).

### Display Update Flow

1. Check if minute changed (skip update if same)
2. Clear buffer, draw time/date/sensor values
3. `EPD_Display()` → `EPD_PartUpdate()` (or `EPD_Update()` for full refresh)
4. Save frame buffer to SD (or SPIFFS fallback)
5. `EPD_DeepSleep()` before entering deep sleep

### Time Management

- NTP server: `ntp.nict.jp`, Timezone: JST (UTC+9)
- Time saved to RTC memory before sleep, restored on wake
- NTP sync at the top of every hour (when `tm_min == 0`)

#### Time Restoration After Deep Sleep

ESP32's system clock is lost during deep sleep. On wake, time is restored from RTC memory:

```
wakeup_time = saved_time + sleep_duration + boot_overhead + drift_compensation
```

**Critical**: All calculations use microsecond precision (int64_t) to avoid truncation drift:
- `savedTime` (seconds) + `savedTimeUs` (microseconds) stored separately
- Integer division truncation caused ~1 second loss per cycle → ~1 minute/hour drift (fixed Dec 2025)

#### RTC Drift Compensation (Dec 2025)

ESP32's internal 150kHz RC oscillator drifts at a temperature-dependent rate. This drift is now actively compensated:

```cpp
// In restoreTimeFromRTC()
drift_compensation = sleep_minutes * driftRateMsPerMin * 1000;  // in microseconds
wakeup_time += drift_compensation;
```

**Drift rate calibration:**
- Default rate: 170 ms/min (initial value)
- Calibrated via NTP sync (every 30 min for debugging, hourly in production)
- True drift = residual + cumulative compensation
- Exponential moving average (70% old + 30% new) for stability
- Clamped to 50-300 ms/min range

**Temperature dependency (observed Dec 2025):**
- Higher temperature → higher drift rate
- ~22.7°C: 110-130 ms/min
- ~21.3°C: 40-60 ms/min
- EMA adapts automatically to temperature changes

**Expected accuracy after compensation:** ~100ms residual per sync cycle

**Logged fields (NTP sync only):**
- `rtc_drift_ms`: Residual drift after compensation
- `cumulative_comp_ms`: Total compensation applied since last sync
- `drift_rate`: Current drift rate used (ms/min)

#### Adaptive Sleep Duration

Goal: Wake up and update display exactly at minute boundary (XX:XX:00).

**Sleep calculation:**
```cpp
sleepMs = (ms until next minute) - estimatedProcessingTime
```

**Feedback loop** (runs when NTP sync not performed):
- If woke too early (had to wait for minute change): decrease `estimatedProcessingTime`
- If woke too late (delay > 0.1s past boundary): increase `estimatedProcessingTime`
- Smoothing factor: 0.5 (gradual adjustment)
- Clamped to 1-20 seconds range

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

## arduwrap Commands

The `scripts/arduwrap` wrapper provides convenient compile and serial log access:

```bash
# Compile and upload (requires serve running in another terminal)
scripts/arduwrap compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDEnvClock

# Get serial log (last 64KB buffered)
scripts/arduwrap log

# Get filtered log (regex pattern)
scripts/arduwrap log --filter "ERROR|WARN"

# Get last N lines
scripts/arduwrap log -n 50

# Clear log buffer
scripts/arduwrap log --clear

# Stop the server
scripts/arduwrap stop
```

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
- Local dev server: `bun run dev` → <http://localhost:4321/>

## Data Analysis Script

Analyze sensor data from D1 database:

```bash
python3 scripts/analyze_data.py [hours]
```

**Authentication Required:**

The script uses `wrangler d1 execute` to query the D1 database. Authentication is required:

1. **Option 1: Cloudflare API Token** (recommended for scripts)
   ```bash
   export CF_API_TOKEN='your-cloudflare-api-token'
   ```
   Get token from: https://dash.cloudflare.com/profile/api-tokens
   - Required permissions: Account → D1 → Read
   - **Note**: Token is NOT stored in `.env` (use environment variable or `wrangler login`)

2. **Option 2: Wrangler Login** (for interactive use)
   ```bash
   cd web
   bunx wrangler login
   ```
   Credentials are stored in `~/.wrangler/config/default.toml`

**Note**: `.env` file contains `CF_ACCESS_CLIENT_ID` and `CF_ACCESS_CLIENT_SECRET` for Cloudflare Access (API authentication), but D1 access requires `CF_API_TOKEN` or `wrangler login`.

**Database Info:**
- Database name: `epd-sensor-db`
- Database ID: `fc27137d-cc9d-48cd-bfc0-5c270356dc98`
- Config: `web/wrangler.toml`

The script analyzes:
- RTC drift values and drift rate calibration
- Cumulative compensation and residual error
- Battery voltage trends and WiFi skip patterns
- Sensor readings (temperature, humidity, CO2)
- WiFi/NTP sync frequency vs battery voltage
- Temperature vs drift rate correlation
