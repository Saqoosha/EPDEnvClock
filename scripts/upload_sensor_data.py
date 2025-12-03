#!/usr/bin/env python3
"""Upload sensor data from JSONL files to the dashboard API."""

import json
import os
import sys
import urllib.request
from pathlib import Path


def load_env_file():
    """Load environment variables from .env file if it exists."""
    # Look for .env in script directory's parent (project root)
    script_dir = Path(__file__).parent
    env_file = script_dir.parent / ".env"

    if env_file.exists():
        with open(env_file) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, value = line.split("=", 1)
                    # Only set if not already in environment
                    if key not in os.environ:
                        os.environ[key] = value


# Load .env file before reading environment variables
load_env_file()

API_URL = os.environ.get("SENSOR_API_URL", "https://epd-sensor-dashboard.pages.dev/api/sensor")
API_KEY = os.environ.get("SENSOR_API_KEY", "")
# Cloudflare Access Service Token (for bypassing Cloudflare Access protection)
CF_ACCESS_CLIENT_ID = os.environ.get("CF_ACCESS_CLIENT_ID", "")
CF_ACCESS_CLIENT_SECRET = os.environ.get("CF_ACCESS_CLIENT_SECRET", "")
BATCH_SIZE = 100  # Upload in batches to avoid timeout


def load_jsonl(file_path: Path) -> list[dict]:
    """Load data from a JSONL file."""
    readings = []
    with open(file_path, "r") as f:
        for line in f:
            line = line.strip()
            if line:
                data = json.loads(line)
                # Convert to API format
                reading = {
                    "timestamp": data["unixtimestamp"],
                    "temp": data["temp"],
                    "humidity": data["humidity"],
                    "co2": data["co2"],
                    "batt_voltage": data.get("batt_voltage"),
                    "batt_percent": data.get("batt_percent"),
                    "batt_rate": data.get("batt_rate"),
                    "charging": data.get("charging"),
                    "batt_adc": data.get("batt_adc"),  # legacy
                }
                if "rtc_drift_ms" in data:
                    reading["rtc_drift_ms"] = data["rtc_drift_ms"]
                readings.append(reading)
    return readings


def upload_batch(readings: list[dict]) -> bool:
    """Upload a batch of readings to the API."""
    data = json.dumps(readings).encode("utf-8")
    headers = {"Content-Type": "application/json"}

    # Add API key if configured
    if API_KEY:
        headers["X-API-Key"] = API_KEY

    # Add Cloudflare Access headers if configured
    if CF_ACCESS_CLIENT_ID and CF_ACCESS_CLIENT_SECRET:
        headers["CF-Access-Client-Id"] = CF_ACCESS_CLIENT_ID
        headers["CF-Access-Client-Secret"] = CF_ACCESS_CLIENT_SECRET

    req = urllib.request.Request(API_URL, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode("utf-8"))
            print(f"  Uploaded {result.get('inserted', len(readings))} readings")
            return True
    except urllib.error.HTTPError as e:
        print(f"  Error: {e.code} - {e.read().decode('utf-8')}")
        return False


def main():
    if len(sys.argv) < 2:
        print("Usage: upload_sensor_data.py <jsonl_file> [jsonl_file2] ...")
        print("\nEnvironment variables:")
        print("  SENSOR_API_URL          - API endpoint URL")
        print("  SENSOR_API_KEY          - API key for authentication")
        print("  CF_ACCESS_CLIENT_ID     - Cloudflare Access Client ID")
        print("  CF_ACCESS_CLIENT_SECRET - Cloudflare Access Client Secret")
        sys.exit(1)

    if not CF_ACCESS_CLIENT_ID or not CF_ACCESS_CLIENT_SECRET:
        print("⚠️  Warning: CF_ACCESS_CLIENT_ID/SECRET not set. May fail if Cloudflare Access is enabled.")
        print("   Set them via environment variables.\n")

    all_readings = []
    for file_path in sys.argv[1:]:
        path = Path(file_path)
        if not path.exists():
            print(f"File not found: {file_path}")
            continue
        print(f"Loading {path.name}...")
        readings = load_jsonl(path)
        print(f"  Loaded {len(readings)} readings")
        all_readings.extend(readings)

    # Sort by timestamp
    all_readings.sort(key=lambda x: x["timestamp"])
    print(f"\nTotal readings to upload: {len(all_readings)}")

    # Upload in batches
    total_uploaded = 0
    for i in range(0, len(all_readings), BATCH_SIZE):
        batch = all_readings[i : i + BATCH_SIZE]
        print(f"Uploading batch {i // BATCH_SIZE + 1} ({len(batch)} readings)...")
        if upload_batch(batch):
            total_uploaded += len(batch)

    print(f"\nDone! Uploaded {total_uploaded} readings total.")


if __name__ == "__main__":
    main()
