-- Sensor data table
CREATE TABLE IF NOT EXISTS sensor_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL UNIQUE,    -- Unix timestamp (unique to prevent duplicates)
    temperature REAL NOT NULL,            -- Temperature in Celsius
    humidity REAL NOT NULL,               -- Humidity in %
    co2 INTEGER NOT NULL,                 -- CO2 in ppm
    battery_voltage REAL,                 -- Battery voltage
    battery_percent REAL,                 -- Battery state of charge (0-100%)
    battery_rate REAL,                    -- Battery charge rate (%/hr, positive=charging)
    battery_charging INTEGER,             -- Charging state (1=charging, 0=not charging)
    battery_adc INTEGER,                  -- Raw ADC value (legacy, deprecated)
    rtc_drift_ms INTEGER,                 -- RTC drift in ms (only when NTP synced)
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);

-- Migration: Add battery_percent and battery_rate columns if they don't exist
-- Run this manually if upgrading from older schema:
-- ALTER TABLE sensor_data ADD COLUMN battery_percent REAL;
-- ALTER TABLE sensor_data ADD COLUMN battery_rate REAL;
-- ALTER TABLE sensor_data ADD COLUMN battery_charging INTEGER;

-- Insert dummy data for testing (last 24 hours, every minute)
-- This will be run manually for development
