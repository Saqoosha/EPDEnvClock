import type { APIRoute } from 'astro';

interface SensorReading {
  timestamp?: number;
  unixtimestamp?: number;  // ESP32 sends this field name
  temp: number;
  humidity: number;
  co2: number;
  batt_voltage?: number;
  batt_percent?: number;
  batt_rate?: number;
  batt_adc?: number;  // legacy, deprecated
  rtc_drift_ms?: number;
}

// POST /api/sensor - Receive sensor data batch from ESP32
export const POST: APIRoute = async ({ request, locals }) => {
  try {
    const env = locals.runtime.env;

    // API Key authentication (check both Cloudflare env and Vite env)
    const serverApiKey = env.API_KEY || import.meta.env.API_KEY;
    const requestApiKey = request.headers.get('X-API-Key') || request.headers.get('Authorization')?.replace('Bearer ', '');

    // Skip auth if no API_KEY is configured (development only warning)
    if (serverApiKey) {
      if (!requestApiKey || requestApiKey !== serverApiKey) {
        return new Response(JSON.stringify({ error: 'Unauthorized' }), {
          status: 401,
          headers: { 'Content-Type': 'application/json' },
        });
      }
    } else {
      console.warn('⚠️ API_KEY not configured - authentication disabled!');
    }

    const db = env.DB;
    const data = await request.json() as SensorReading | SensorReading[];

    // Handle both single reading and batch
    const readings = Array.isArray(data) ? data : [data];

    if (readings.length === 0) {
      return new Response(JSON.stringify({ error: 'No data provided' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' },
      });
    }

    // Validate and insert all readings (ignore duplicates)
    const stmt = db.prepare(`
      INSERT OR IGNORE INTO sensor_data (timestamp, temperature, humidity, co2, battery_voltage, battery_percent, battery_rate, rtc_drift_ms)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `);

    const batch = readings.map((r, i) => {
      const ts = r.timestamp ?? r.unixtimestamp;

      // Validate required fields
      if (ts === undefined || ts === null) {
        throw new Error(`Record ${i}: missing timestamp (timestamp=${r.timestamp}, unixtimestamp=${r.unixtimestamp})`);
      }
      if (r.temp === undefined || r.temp === null) {
        throw new Error(`Record ${i}: missing temp`);
      }
      if (r.humidity === undefined || r.humidity === null) {
        throw new Error(`Record ${i}: missing humidity`);
      }
      if (r.co2 === undefined || r.co2 === null) {
        throw new Error(`Record ${i}: missing co2`);
      }

      return stmt.bind(ts, r.temp, r.humidity, r.co2, r.batt_voltage ?? null, r.batt_percent ?? null, r.batt_rate ?? null, r.rtc_drift_ms ?? null);
    });

    await db.batch(batch);

    return new Response(JSON.stringify({
      success: true,
      inserted: readings.length
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    });

  } catch (error) {
    console.error('Error inserting sensor data:', error);
    return new Response(JSON.stringify({
      error: 'Failed to insert data',
      details: error instanceof Error ? error.message : 'Unknown error'
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    });
  }
};
