#!/usr/bin/env npx tsx

/**
 * Send dummy sensor data to the API endpoint
 * Usage: npx tsx scripts/send-dummy-data.ts [options]
 *
 * Options:
 *   --url=<url>     API base URL (default: http://localhost:4321)
 *   --count=<n>     Number of data points to send (default: 60)
 *   --interval=<s>  Interval between points in seconds (default: 60)
 */

interface SensorReading {
  timestamp: number;
  temp: number;
  humidity: number;
  co2: number;
  batt_voltage: number;
  batt_adc: number;
}

function parseArgs(): { url: string; count: number; interval: number } {
  const args = process.argv.slice(2);
  let url = 'http://localhost:4321';
  let count = 60;
  let interval = 60;

  for (const arg of args) {
    if (arg.startsWith('--url=')) {
      url = arg.slice(6);
    } else if (arg.startsWith('--count=')) {
      count = parseInt(arg.slice(8), 10);
    } else if (arg.startsWith('--interval=')) {
      interval = parseInt(arg.slice(11), 10);
    }
  }

  return { url, count, interval };
}

function generateDummyData(count: number, intervalSeconds: number): SensorReading[] {
  const now = Math.floor(Date.now() / 1000);
  const data: SensorReading[] = [];

  for (let i = 0; i < count; i++) {
    const timestamp = now - (count - 1 - i) * intervalSeconds;
    const hourOfDay = new Date(timestamp * 1000).getHours();

    // Simulate daily patterns
    const tempBase = 23 + 3 * Math.sin((hourOfDay - 6) / 24 * 2 * Math.PI);
    const humidityBase = 50 - 10 * Math.sin((hourOfDay - 6) / 24 * 2 * Math.PI);
    const co2Base = hourOfDay >= 9 && hourOfDay <= 18
      ? 600 + 300 * Math.sin((hourOfDay - 9) / 9 * Math.PI)
      : 450;

    data.push({
      timestamp,
      temp: Math.round((tempBase + (Math.random() - 0.5)) * 10) / 10,
      humidity: Math.round((humidityBase + (Math.random() - 0.5) * 5) * 10) / 10,
      co2: Math.round(co2Base + (Math.random() - 0.5) * 50),
      batt_voltage: Math.round((4.0 + Math.random() * 0.2) * 1000) / 1000,
      batt_adc: Math.round(2300 + Math.random() * 100),
    });
  }

  return data;
}

async function sendData(url: string, data: SensorReading[]): Promise<void> {
  const endpoint = `${url}/api/sensor`;

  console.log(`Sending ${data.length} data points to ${endpoint}...`);
  console.log(`Time range: ${new Date(data[0].timestamp * 1000).toLocaleString()} - ${new Date(data[data.length - 1].timestamp * 1000).toLocaleString()}`);

  const response = await fetch(endpoint, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
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
  const { url, count, interval } = parseArgs();

  console.log(`\n📊 Dummy Data Sender`);
  console.log(`   URL: ${url}`);
  console.log(`   Count: ${count} points`);
  console.log(`   Interval: ${interval}s\n`);

  const data = generateDummyData(count, interval);

  // Show sample data
  console.log('Sample data (first 3):');
  data.slice(0, 3).forEach((d, i) => {
    console.log(`  [${i}] ${new Date(d.timestamp * 1000).toLocaleTimeString()} - temp: ${d.temp}°C, humidity: ${d.humidity}%, co2: ${d.co2}ppm`);
  });
  console.log('  ...\n');

  await sendData(url, data);
}

main().catch(console.error);
