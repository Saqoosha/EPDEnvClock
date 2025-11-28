#!/usr/bin/env python3
"""Upload sensor data from JSONL files to the dashboard API."""

import json
import sys
import urllib.request
from pathlib import Path

API_URL = "https://epd-sensor-dashboard.pages.dev/api/sensor"
API_KEY = "test-secret-key-123"
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
                    "batt_adc": data.get("batt_adc"),
                }
                if "rtc_drift_ms" in data:
                    reading["rtc_drift_ms"] = data["rtc_drift_ms"]
                readings.append(reading)
    return readings


def upload_batch(readings: list[dict]) -> bool:
    """Upload a batch of readings to the API."""
    data = json.dumps(readings).encode("utf-8")
    req = urllib.request.Request(
        API_URL,
        data=data,
        headers={
            "Content-Type": "application/json",
            "X-API-Key": API_KEY,
        },
        method="POST",
    )
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
        sys.exit(1)

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
