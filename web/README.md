# EPDEnvClock Dashboard

Sensor data visualization dashboard for the EPDEnvClock device.

- **Stack**: Astro + Cloudflare Pages + D1 (SQLite)
- **Charts**: Temperature, Humidity, CO2, Battery

## ESP32 Setup

The ESP32 automatically uploads sensor data to the server every ~60 minutes (when NTP sync occurs).

### 1. Create secrets.h

Copy the example file and configure:

```bash
cp EPDEnvClock/secrets.h.example EPDEnvClock/secrets.h
```

Edit `secrets.h`:

```c
#pragma once

// Sensor Logger API URL (without trailing slash)
#define SENSOR_API_URL "https://epd-sensor-dashboard.pages.dev"

// API key for server authentication
#define API_KEY "your-api-key-here"
```

### 2. How it works

- Sensor readings are logged to SD card every minute (`/sensor_logs/sensor_log_YYYYMMDD.jsonl`)
- Every 60 boots (~60 min), when WiFi/NTP sync occurs:
  - ESP32 reads unsent data from SD card (up to 60 records)
  - Sends batch POST to `/api/sensor`
  - Updates `lastUploadedTime` in RTC memory to track progress
- Duplicate records (same timestamp) are automatically ignored by the server

### 3. Log file format (JSONL)

```json
{"date":"2025.11.28","time":"12:00:00","unixtimestamp":1732780800,"temp":23.5,"humidity":45.0,"co2":650,"batt_adc":2400,"batt_voltage":4.2}
{"date":"2025.11.28","time":"12:01:00","unixtimestamp":1732780860,"temp":23.6,"humidity":44.8,"co2":655,"batt_adc":2395,"batt_voltage":4.19}
```

## Local Development

### Prerequisites

- [Bun](https://bun.sh/) v1.0+

### Setup

```bash
cd web
bun install
```

### Create local D1 database

```bash
bunx wrangler d1 execute epd-sensor-db --local --file=schema.sql
```

### Seed dummy data (optional)

```bash
bunx wrangler d1 execute epd-sensor-db --local --file=seed-dummy-data.sql
```

### Configure API Key (optional)

Create `.dev.vars` file:

```
API_KEY=your-secret-key
```

### Run dev server

```bash
bun run dev
```

Open <http://localhost:4321>

### Send test data

```bash
bun run scripts/send-dummy-data.ts --count=60 --api-key=your-secret-key
```

## Cloudflare Deployment

### 1. Login to Cloudflare

```bash
bunx wrangler login
```

### 2. Create D1 Database

```bash
bunx wrangler d1 create epd-sensor-db
```

Copy the `database_id` from the output and update `wrangler.toml`:

```toml
[[d1_databases]]
binding = "DB"
database_name = "epd-sensor-db"
database_id = "your-database-id-here"
```

### 3. Apply Schema to Production DB

```bash
bunx wrangler d1 execute epd-sensor-db --remote --file=schema.sql
```

### 4. Create Pages Project

```bash
bunx wrangler pages project create epd-sensor-dashboard --production-branch main
```

### 5. Build & Deploy

```bash
bun run build
bunx wrangler pages deploy --commit-dirty=true
```

### 6. Set API Key Secret

```bash
echo "your-production-api-key" | bunx wrangler pages secret put API_KEY --project-name epd-sensor-dashboard
```

### 7. Verify Deployment

```bash
# Send test data
bun run scripts/send-dummy-data.ts \
  --url=https://your-project.pages.dev \
  --count=5 \
  --api-key=your-production-api-key
```

## API Endpoints

### POST /api/sensor

Receive sensor data from ESP32.

**Headers:**

- `X-API-Key`: Your API key (required in production)
- `Content-Type`: application/json

**Request Body:**

Single reading:

```json
{
  "timestamp": 1732700000,
  "temp": 23.5,
  "humidity": 45.2,
  "co2": 650,
  "batt_voltage": 4.1,
  "batt_adc": 2380
}
```

Batch (recommended for ESP32):

```json
[
  { "timestamp": 1732700000, "temp": 23.5, "humidity": 45.2, "co2": 650 },
  { "timestamp": 1732700060, "temp": 23.6, "humidity": 45.0, "co2": 655 }
]
```

**Field notes:**

- `timestamp` or `unixtimestamp`: Unix timestamp (both accepted, ESP32 sends `unixtimestamp`)
- `temp`: Temperature in Celsius
- `humidity`: Humidity in %
- `co2`: CO2 concentration in ppm
- `batt_voltage`: Battery voltage (optional)
- `batt_adc`: Raw ADC value (optional)
- `rtc_drift_ms`: RTC drift in milliseconds (optional, only when NTP synced)

**Duplicate handling:**

- Records with the same timestamp are silently ignored (`INSERT OR IGNORE`)
- This allows safe retries without creating duplicate data

**Response:**

```json
{ "success": true, "inserted": 60 }
```

**Error Response:**

```json
{ "error": "Failed to insert data", "details": "error message" }
```

### GET /api/data

Fetch sensor data for charts.

**Query Parameters:**

- `hours`: Number of hours to fetch (default: 24)
- `from`: Unix timestamp start
- `to`: Unix timestamp end

**Example:**

```
GET /api/data?hours=24
GET /api/data?from=1732600000&to=1732700000
```

## Project Structure

```
web/
├── src/
│   ├── pages/
│   │   ├── index.astro      # Dashboard UI
│   │   └── api/
│   │       ├── sensor.ts    # POST: receive sensor data
│   │       └── data.ts      # GET: fetch data for charts
│   └── env.d.ts             # TypeScript types
├── scripts/
│   └── send-dummy-data.ts   # Test data sender
├── schema.sql               # D1 table definition
├── seed-dummy-data.sql      # Dummy data generator
├── wrangler.toml            # Cloudflare config
├── astro.config.mjs         # Astro config
└── .dev.vars.example        # Environment template
```

## Updating API Key

```bash
# Production
echo "new-secret-key" | bunx wrangler pages secret put API_KEY --project-name epd-sensor-dashboard

# Local (.dev.vars)
echo "API_KEY=new-secret-key" > .dev.vars
```

## Redeploying

```bash
bun run build
bunx wrangler pages deploy --commit-dirty=true
```
