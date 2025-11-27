import type { APIRoute } from 'astro';

interface SensorReading {
  timestamp: number;
  temp: number;
  humidity: number;
  co2: number;
  batt_voltage?: number;
  batt_adc?: number;
}

// POST /api/sensor - Receive sensor data batch from ESP32
export const POST: APIRoute = async ({ request, locals }) => {
  try {
    const db = locals.runtime.env.DB;
    const data = await request.json() as SensorReading | SensorReading[];

    // Handle both single reading and batch
    const readings = Array.isArray(data) ? data : [data];

    if (readings.length === 0) {
      return new Response(JSON.stringify({ error: 'No data provided' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' },
      });
    }

    // Insert all readings
    const stmt = db.prepare(`
      INSERT INTO sensor_data (timestamp, temperature, humidity, co2, battery_voltage, battery_adc)
      VALUES (?, ?, ?, ?, ?, ?)
    `);

    const batch = readings.map(r =>
      stmt.bind(r.timestamp, r.temp, r.humidity, r.co2, r.batt_voltage ?? null, r.batt_adc ?? null)
    );

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
