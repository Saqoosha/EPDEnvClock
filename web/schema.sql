-- Sensor data table
CREATE TABLE IF NOT EXISTS sensor_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,           -- Unix timestamp
    temperature REAL NOT NULL,            -- Temperature in Celsius
    humidity REAL NOT NULL,               -- Humidity in %
    co2 INTEGER NOT NULL,                 -- CO2 in ppm
    battery_voltage REAL,                 -- Battery voltage
    battery_adc INTEGER,                  -- Raw ADC value
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);

-- Index for efficient time-range queries
CREATE INDEX IF NOT EXISTS idx_sensor_timestamp ON sensor_data(timestamp);

-- Insert dummy data for testing (last 24 hours, every minute)
-- This will be run manually for development
