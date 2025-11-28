import type { APIRoute } from 'astro';

interface Stats {
  temperature: { min: number | null; max: number | null };
  humidity: { min: number | null; max: number | null };
  co2: { min: number | null; max: number | null };
  battery_voltage: { min: number | null; max: number | null };
}

// GET /api/data - Fetch sensor data for charts
// Query params:
//   hours: number of hours to fetch (default: 24)
//   from: unix timestamp start
//   to: unix timestamp end
export const GET: APIRoute = async ({ url, locals }) => {
  try {
    const db = locals.runtime.env.DB;
    const params = url.searchParams;

    let dataQuery: string;
    let statsQuery: string;
    let bindings: (number | null)[];

    const fromTs = params.get('from');
    const toTs = params.get('to');
    const hours = params.get('hours');

    if (fromTs && toTs) {
      // Specific time range
      dataQuery = `
        SELECT timestamp, temperature, humidity, co2, battery_voltage, rtc_drift_ms
        FROM sensor_data
        WHERE timestamp >= ? AND timestamp <= ?
        ORDER BY timestamp ASC
      `;
      statsQuery = `
        SELECT
          MIN(temperature) as temp_min, MAX(temperature) as temp_max,
          MIN(humidity) as hum_min, MAX(humidity) as hum_max,
          MIN(co2) as co2_min, MAX(co2) as co2_max,
          MIN(battery_voltage) as bat_min, MAX(battery_voltage) as bat_max
        FROM sensor_data
        WHERE timestamp >= ? AND timestamp <= ?
      `;
      bindings = [parseInt(fromTs), parseInt(toTs)];
    } else {
      // Last N hours (default 24)
      const hoursNum = hours ? parseInt(hours) : 24;
      const nowTs = Math.floor(Date.now() / 1000);
      const startTs = nowTs - (hoursNum * 60 * 60);

      dataQuery = `
        SELECT timestamp, temperature, humidity, co2, battery_voltage, rtc_drift_ms
        FROM sensor_data
        WHERE timestamp >= ?
        ORDER BY timestamp ASC
      `;
      statsQuery = `
        SELECT
          MIN(temperature) as temp_min, MAX(temperature) as temp_max,
          MIN(humidity) as hum_min, MAX(humidity) as hum_max,
          MIN(co2) as co2_min, MAX(co2) as co2_max,
          MIN(battery_voltage) as bat_min, MAX(battery_voltage) as bat_max
        FROM sensor_data
        WHERE timestamp >= ?
      `;
      bindings = [startTs];
    }

    const [dataResult, statsResult] = await Promise.all([
      db.prepare(dataQuery).bind(...bindings).all(),
      db.prepare(statsQuery).bind(...bindings).first()
    ]);

    const stats: Stats = {
      temperature: {
        min: statsResult?.temp_min as number | null,
        max: statsResult?.temp_max as number | null
      },
      humidity: {
        min: statsResult?.hum_min as number | null,
        max: statsResult?.hum_max as number | null
      },
      co2: {
        min: statsResult?.co2_min as number | null,
        max: statsResult?.co2_max as number | null
      },
      battery_voltage: {
        min: statsResult?.bat_min as number | null,
        max: statsResult?.bat_max as number | null
      },
    };

    return new Response(JSON.stringify({
      success: true,
      count: dataResult.results.length,
      data: dataResult.results,
      stats,
    }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        'Cache-Control': 'public, max-age=60', // Cache for 1 minute
      },
    });

  } catch (error) {
    console.error('Error fetching sensor data:', error);
    return new Response(JSON.stringify({
      error: 'Failed to fetch data',
      details: error instanceof Error ? error.message : 'Unknown error'
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    });
  }
};
