-- Seed dummy data: 24 hours of sensor readings (every minute = 1440 rows)
-- Temperature: 20-26°C with daily cycle
-- Humidity: 40-60% inverse to temperature
-- CO2: 400-1200 ppm with peaks during "occupied" hours

-- Clear existing data
DELETE FROM sensor_data;

-- Generate dummy data using recursive CTE
WITH RECURSIVE
  time_series(n) AS (
    SELECT 0
    UNION ALL
    SELECT n + 1 FROM time_series WHERE n < 1439
  ),
  base_time AS (
    SELECT strftime('%s', 'now', '-24 hours') AS start_ts
  )
INSERT INTO sensor_data (timestamp, temperature, humidity, co2, battery_voltage, battery_adc)
SELECT
  CAST(b.start_ts AS INTEGER) + (t.n * 60) AS timestamp,
  -- Temperature: base 23°C, +/-3°C daily cycle, +/-0.5°C noise
  ROUND(23.0 + 3.0 * SIN((t.n / 1440.0) * 2 * 3.14159 - 1.5) + (RANDOM() % 100) / 200.0, 1) AS temperature,
  -- Humidity: base 50%, inverse to temp cycle, +/-5% noise
  ROUND(50.0 - 10.0 * SIN((t.n / 1440.0) * 2 * 3.14159 - 1.5) + (RANDOM() % 100) / 20.0, 1) AS humidity,
  -- CO2: base 500, peaks 800-1200 during day (minutes 360-1080), noise
  CAST(
    CASE
      WHEN t.n BETWEEN 360 AND 1080 THEN
        600 + 400 * SIN(((t.n - 360) / 720.0) * 3.14159) + ABS(RANDOM() % 100)
      ELSE
        450 + ABS(RANDOM() % 80)
    END AS INTEGER
  ) AS co2,
  -- Battery: slowly decreasing from 4.2V to 3.8V
  ROUND(4.2 - (t.n / 1440.0) * 0.4 + (RANDOM() % 100) / 2000.0, 3) AS battery_voltage,
  -- ADC: calculated from voltage using inverse of calibration formula
  CAST((4.2 - (t.n / 1440.0) * 0.4 + 1.353) / 0.002334 AS INTEGER) AS battery_adc
FROM time_series t, base_time b;
