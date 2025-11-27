# EPDEnvClock Dashboard

Sensor data visualization dashboard for the EPDEnvClock device.

- **Stack**: Astro + Cloudflare Pages + D1 (SQLite)
- **Charts**: Temperature, Humidity, CO2, Battery

## Local Development

### Prerequisites

- Node.js 18+
- npm

### Setup

```bash
cd web
npm install
```

### Create local D1 database

```bash
npx wrangler d1 execute epd-sensor-db --local --file=schema.sql
```

### Seed dummy data (optional)

```bash
npx wrangler d1 execute epd-sensor-db --local --file=seed-dummy-data.sql
```

### Configure API Key (optional)

Create `.dev.vars` file:

```
API_KEY=your-secret-key
```

### Run dev server

```bash
npm run dev
```

Open http://localhost:4321

### Send test data

```bash
npx tsx scripts/send-dummy-data.ts --count=60 --api-key=your-secret-key
```

## Cloudflare Deployment

### 1. Login to Cloudflare

```bash
npx wrangler login
```

### 2. Create D1 Database

```bash
npx wrangler d1 create epd-sensor-db
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
npx wrangler d1 execute epd-sensor-db --remote --file=schema.sql
```

### 4. Create Pages Project

```bash
npx wrangler pages project create epd-sensor-dashboard --production-branch main
```

### 5. Build & Deploy

```bash
npm run build
npx wrangler pages deploy --commit-dirty=true
```

### 6. Set API Key Secret

```bash
echo "your-production-api-key" | npx wrangler pages secret put API_KEY --project-name epd-sensor-dashboard
```

### 7. Verify Deployment

```bash
# Send test data
npx tsx scripts/send-dummy-data.ts \
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

**Response:**
```json
{ "success": true, "inserted": 60 }
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
echo "new-secret-key" | npx wrangler pages secret put API_KEY --project-name epd-sensor-dashboard

# Local (.dev.vars)
echo "API_KEY=new-secret-key" > .dev.vars
```

## Redeploying

```bash
npm run build
npx wrangler pages deploy --commit-dirty=true
```
