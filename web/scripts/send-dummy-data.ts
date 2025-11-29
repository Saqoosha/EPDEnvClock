#!/usr/bin/env npx tsx

/**
 * Send dummy sensor data to the API endpoint
 * Usage: npx tsx scripts/send-dummy-data.ts [options]
 *
 * Options:
 *   --url=<url>       API base URL (default: http://localhost:4321)
 *   --count=<n>       Number of data points to send (default: 60)
 *   --interval=<s>    Interval between points in seconds (default: 60)
 *   --api-key=<key>   API key for authentication (or set API_KEY env var)
 */

interface SensorReading {
  timestamp: number;
  temp: number;
  humidity: number;
  co2: number;
  batt_voltage: number;
  batt_percent: number;
  batt_rate: number;
  rtc_drift_ms?: number;
}

function parseArgs(): { url: string; count: number; interval: number; apiKey: string } {
  const args = process.argv.slice(2);
  let url = 'http://localhost:4321';
  let count = 60;
  let interval = 60;
  let apiKey = process.env.API_KEY || '';

  for (const arg of args) {
    if (arg.startsWith('--url=')) {
      url = arg.slice(6);
    } else if (arg.startsWith('--count=')) {
      count = parseInt(arg.slice(8), 10);
    } else if (arg.startsWith('--interval=')) {
      interval = parseInt(arg.slice(11), 10);
    } else if (arg.startsWith('--api-key=')) {
      apiKey = arg.slice(10);
    }
  }

  return { url, count, interval, apiKey };
}

function generateDummyData(count: number, intervalSeconds: number): SensorReading[] {
  const now = Math.floor(Date.now() / 1000);
  const data: SensorReading[] = [];

  for (let i = 0; i < count; i++) {
    const timestamp = now - (count - 1 - i) * intervalSeconds;
    const hourOfDay = new Date(timestamp * 1000).getHours();
    const minuteOfHour = new Date(timestamp * 1000).getMinutes();

    // Simulate daily patterns
    const tempBase = 23 + 3 * Math.sin((hourOfDay - 6) / 24 * 2 * Math.PI);
    const humidityBase = 50 - 10 * Math.sin((hourOfDay - 6) / 24 * 2 * Math.PI);
    const co2Base = hourOfDay >= 9 && hourOfDay <= 18
      ? 600 + 300 * Math.sin((hourOfDay - 9) / 9 * Math.PI)
      : 450;

    // Battery simulation: slowly draining from 100% to 80% over the period
    const batteryPercent = 100 - (i / count) * 20 + (Math.random() - 0.5) * 2;
    const batteryVoltage = 3.7 + (batteryPercent / 100) * 0.5; // 3.7V at 0%, 4.2V at 100%

    const reading: SensorReading = {
      timestamp,
      temp: Math.round((tempBase + (Math.random() - 0.5)) * 10) / 10,
      humidity: Math.round((humidityBase + (Math.random() - 0.5) * 5) * 10) / 10,
      co2: Math.round(co2Base + (Math.random() - 0.5) * 50),
      batt_voltage: Math.round(batteryVoltage * 1000) / 1000,
      batt_percent: Math.round(batteryPercent * 10) / 10,
      batt_rate: Math.round((-20 / count * 60 + (Math.random() - 0.5)) * 100) / 100, // %/hr
    };

    // Add RTC drift once per hour (at minute 0)
    // Simulate drift: -500ms to +500ms, with slight positive bias (clock runs fast)
    if (minuteOfHour === 0 || (intervalSeconds >= 3600)) {
      reading.rtc_drift_ms = Math.round((Math.random() - 0.4) * 1000);
    }

    data.push(reading);
  }

  return data;
}

async function sendData(url: string, data: SensorReading[], apiKey: string): Promise<void> {
  const endpoint = `${url}/api/sensor`;

  console.log(`Sending ${data.length} data points to ${endpoint}...`);
  console.log(`Time range: ${new Date(data[0].timestamp * 1000).toLocaleString()} - ${new Date(data[data.length - 1].timestamp * 1000).toLocaleString()}`);

  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  };

  if (apiKey) {
    headers['X-API-Key'] = apiKey;
  }

  const response = await fetch(endpoint, {
    method: 'POST',
    headers,
    body: JSON.stringify(data),
  });

  const result = await response.json();

  if (response.ok) {
    console.log('✅ Success:', result);
  } else {
    console.error('❌ Error:', result);
    process.exit(1);
  }
}

async function main() {
  const { url, count, interval, apiKey } = parseArgs();

  console.log(`\n📊 Dummy Data Sender`);
  console.log(`   URL: ${url}`);
  console.log(`   Count: ${count} points`);
  console.log(`   Interval: ${interval}s`);
  console.log(`   API Key: ${apiKey ? '****' + apiKey.slice(-4) : '(none)'}\n`);

  const data = generateDummyData(count, interval);

  // Show sample data
  console.log('Sample data (first 3):');
  data.slice(0, 3).forEach((d, i) => {
    console.log(`  [${i}] ${new Date(d.timestamp * 1000).toLocaleTimeString()} - temp: ${d.temp}°C, humidity: ${d.humidity}%, co2: ${d.co2}ppm`);
  });
  console.log('  ...\n');

  await sendData(url, data, apiKey);
}

main().catch(console.error);
