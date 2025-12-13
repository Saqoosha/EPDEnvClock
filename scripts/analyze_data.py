#!/usr/bin/env python3
"""
Analyze latest sensor data from D1 database via wrangler
"""
import json
import sys
import subprocess
import os
from datetime import datetime
from typing import List, Dict, Any

DATABASE_ID = "fc27137d-cc9d-48cd-bfc0-5c270356dc98"
DATABASE_NAME = "epd-sensor-db"


def fetch_data(hours: int = 48) -> Dict[str, Any]:
    """Fetch sensor data from D1 database via wrangler"""
    now_ts = int(datetime.now().timestamp())
    start_ts = now_ts - (hours * 3600)

    query = f"""
    SELECT timestamp, temperature, humidity, co2, battery_voltage, battery_percent, battery_rate, battery_charging, rtc_drift_ms
    FROM sensor_data
    WHERE timestamp >= {start_ts}
    ORDER BY timestamp ASC
    """

    cmd = [
        "bunx", "wrangler", "d1", "execute", DATABASE_NAME,
        "--remote",
        "--command", query
    ]

    # Use CF_API_TOKEN if available
    env = os.environ.copy()

    # Check if CF_API_TOKEN is set
    if not env.get('CF_API_TOKEN') and not env.get('CLOUDFLARE_API_TOKEN'):
        print("⚠️  CF_API_TOKEN not set. Using wrangler login credentials if available.", file=sys.stderr)
        print("   To use API token: export CF_API_TOKEN='your-token'", file=sys.stderr)

    try:
        result = subprocess.run(
            cmd,
            cwd="web",
            capture_output=True,
            text=True,
            env=env,
            timeout=30
        )

        if result.returncode != 0:
            print(f"Error executing wrangler: {result.stderr}", file=sys.stderr)
            if "not authorized" in result.stderr.lower() or "7403" in result.stderr or "not valid" in result.stderr.lower():
                print("\n⚠️  Authentication required:", file=sys.stderr)
                print("   1. Set CF_API_TOKEN: export CF_API_TOKEN='your-cloudflare-api-token'", file=sys.stderr)
                print("   2. Or run: cd web && bunx wrangler login", file=sys.stderr)
                print("\n   Get API token from: https://dash.cloudflare.com/profile/api-tokens", file=sys.stderr)
            sys.exit(1)

        # Parse wrangler output (JSON)
        output = result.stdout
        # Find JSON in output (wrangler may add extra text)
        json_start = output.find('[')
        json_end = output.rfind(']') + 1

        if json_start == -1:
            print(f"Error: No JSON found in output: {output[:200]}", file=sys.stderr)
            sys.exit(1)

        wrangler_output = json.loads(output[json_start:json_end])

        # Extract results from wrangler format
        if isinstance(wrangler_output, list) and len(wrangler_output) > 0:
            if 'results' in wrangler_output[0]:
                data = wrangler_output[0]['results']
                return {
                    'success': True,
                    'data': data,
                    'count': len(data)
                }

        return {'success': True, 'data': [], 'count': 0}

    except subprocess.TimeoutExpired:
        print("Error: wrangler command timed out", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        print(f"Output: {output[:500]}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error fetching data: {e}", file=sys.stderr)
        sys.exit(1)


def analyze_rtc_drift(data: List[Dict]) -> None:
    """Analyze RTC drift values"""
    drifts = [d['rtc_drift_ms'] for d in data if d.get('rtc_drift_ms') is not None]
    if not drifts:
        print("No RTC drift data available")
        return

    print(f"\n📊 RTC Drift Analysis ({len(drifts)} measurements)")
    print(f"  Min: {min(drifts):.0f} ms")
    print(f"  Max: {max(drifts):.0f} ms")
    print(f"  Avg: {sum(drifts)/len(drifts):.1f} ms")
    print(f"  Median: {sorted(drifts)[len(drifts)//2]:.0f} ms")

    # Group analysis
    low_drift = [d for d in drifts if d < 10000]
    high_drift = [d for d in drifts if d >= 10000]

    if low_drift:
        print(f"\n  Low drift group (<10s): {len(low_drift)} measurements")
        print(f"    Avg: {sum(low_drift)/len(low_drift):.1f} ms")
    if high_drift:
        print(f"\n  High drift group (>=10s): {len(high_drift)} measurements")
        print(f"    Avg: {sum(high_drift)/len(high_drift):.1f} ms")


def analyze_battery(data: List[Dict]) -> None:
    """Analyze battery data"""
    voltages = [d['battery_voltage'] for d in data if d.get('battery_voltage') is not None]
    percents = [d['battery_percent'] for d in data if d.get('battery_percent') is not None]
    charging = [d['battery_charging'] for d in data if d.get('battery_charging') is not None]

    if not voltages:
        print("No battery data available")
        return

    print(f"\n🔋 Battery Analysis ({len(voltages)} measurements)")
    print(f"  Voltage: {min(voltages):.3f}V - {max(voltages):.3f}V (avg: {sum(voltages)/len(voltages):.3f}V)")

    if percents:
        print(f"  Percent: {min(percents):.1f}% - {max(percents):.1f}% (avg: {sum(percents)/len(percents):.1f}%)")

    if charging:
        charging_count = sum(1 for c in charging if c == 1)
        print(f"  Charging: {charging_count}/{len(charging)} measurements ({charging_count*100/len(charging):.1f}%)")

    # Voltage trend
    if len(voltages) > 10:
        recent_avg = sum(voltages[-10:]) / 10
        older_avg = sum(voltages[:10]) / 10
        trend = recent_avg - older_avg
        print(f"  Trend: {trend:+.3f}V (recent vs older)")


def analyze_sensors(data: List[Dict]) -> None:
    """Analyze sensor readings"""
    temps = [d['temperature'] for d in data if d.get('temperature') is not None]
    humidity = [d['humidity'] for d in data if d.get('humidity') is not None]
    co2 = [d['co2'] for d in data if d.get('co2') is not None]

    print(f"\n🌡️  Sensor Analysis ({len(data)} measurements)")

    if temps:
        print(f"  Temperature: {min(temps):.1f}°C - {max(temps):.1f}°C (avg: {sum(temps)/len(temps):.1f}°C)")

    if humidity:
        print(f"  Humidity: {min(humidity):.1f}% - {max(humidity):.1f}% (avg: {sum(humidity)/len(humidity):.1f}%)")

    if co2:
        print(f"  CO2: {min(co2)}ppm - {max(co2)}ppm (avg: {sum(co2)/len(co2):.0f}ppm)")


def analyze_wifi_skips(data: List[Dict]) -> None:
    """Analyze WiFi skip patterns (based on RTC drift pattern)"""
    # RTC drift is only measured when NTP sync happens
    # If drift is missing, WiFi might have been skipped
    total = len(data)
    with_drift = sum(1 for d in data if d.get('rtc_drift_ms') is not None)
    without_drift = total - with_drift

    print(f"\n📡 WiFi/NTP Sync Analysis")
    print(f"  Total measurements: {total}")
    print(f"  With RTC drift (NTP synced): {with_drift} ({with_drift*100/total:.1f}%)")
    print(f"  Without RTC drift (WiFi skipped?): {without_drift} ({without_drift*100/total:.1f}%)")

    # Check if missing drifts correlate with low battery
    if without_drift > 0:
        no_drift_data = [d for d in data if d.get('rtc_drift_ms') is None]
        voltages_no_drift = [d['battery_voltage'] for d in no_drift_data if d.get('battery_voltage') is not None]
        if voltages_no_drift:
            avg_voltage_no_drift = sum(voltages_no_drift) / len(voltages_no_drift)
            print(f"  Avg voltage when drift missing: {avg_voltage_no_drift:.3f}V")


def main():
    hours = int(sys.argv[1]) if len(sys.argv) > 1 else 48
    print(f"Fetching last {hours} hours of data...")

    result = fetch_data(hours)

    if not result.get('success'):
        print(f"API error: {result.get('error', 'Unknown error')}", file=sys.stderr)
        sys.exit(1)

    data = result.get('data', [])
    count = result.get('count', 0)

    if not data:
        print("No data available")
        return

    print(f"\n✅ Loaded {count} data points")

    # Time range
    if data:
        first_ts = data[0]['timestamp']
        last_ts = data[-1]['timestamp']
        first_dt = datetime.fromtimestamp(first_ts)
        last_dt = datetime.fromtimestamp(last_ts)
        print(f"  Time range: {first_dt.strftime('%Y-%m-%d %H:%M:%S')} - {last_dt.strftime('%Y-%m-%d %H:%M:%S')}")

    # Run analyses
    analyze_sensors(data)
    analyze_battery(data)
    analyze_rtc_drift(data)
    analyze_wifi_skips(data)

    print()


if __name__ == '__main__':
    main()
