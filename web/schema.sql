-- Sensor data table
CREATE TABLE IF NOT EXISTS sensor_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL UNIQUE,    -- Unix timestamp (unique to prevent duplicates)
    temperature REAL NOT NULL,            -- Temperature in Celsius
    humidity REAL NOT NULL,               -- Humidity in %
    co2 INTEGER NOT NULL,                 -- CO2 in ppm
    battery_voltage REAL,                 -- Battery voltage
    battery_adc INTEGER,                  -- Raw ADC value
    rtc_drift_ms INTEGER,                 -- RTC drift in ms (only when NTP synced)
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);

-- Insert dummy data for testing (last 24 hours, every minute)
-- This will be run manually for development
