import type { APIRoute } from 'astro';

// GET /api/data - Fetch sensor data for charts
// Query params:
//   hours: number of hours to fetch (default: 24)
//   from: unix timestamp start
//   to: unix timestamp end
export const GET: APIRoute = async ({ url, locals }) => {
  try {
    const db = locals.runtime.env.DB;
    const params = url.searchParams;

    let query: string;
    let bindings: (number | null)[];

    const fromTs = params.get('from');
    const toTs = params.get('to');
    const hours = params.get('hours');

    if (fromTs && toTs) {
      // Specific time range
      query = `
        SELECT timestamp, temperature, humidity, co2, battery_voltage, rtc_drift_ms
        FROM sensor_data
        WHERE timestamp >= ? AND timestamp <= ?
        ORDER BY timestamp ASC
      `;
      bindings = [parseInt(fromTs), parseInt(toTs)];
    } else {
      // Last N hours (default 24)
      const hoursNum = hours ? parseInt(hours) : 24;
      const nowTs = Math.floor(Date.now() / 1000);
      const startTs = nowTs - (hoursNum * 60 * 60);

      query = `
        SELECT timestamp, temperature, humidity, co2, battery_voltage, rtc_drift_ms
        FROM sensor_data
        WHERE timestamp >= ?
        ORDER BY timestamp ASC
      `;
      bindings = [startTs];
    }

    const result = await db.prepare(query).bind(...bindings).all();

    return new Response(JSON.stringify({
      success: true,
      count: result.results.length,
      data: result.results,
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
