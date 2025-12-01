-- Seed dummy data: 24 hours of sensor readings (every minute = 1440 rows)
-- Temperature: 20-26°C with daily cycle
-- Humidity: 40-60% inverse to temperature
-- CO2: 400-1200 ppm with peaks during "occupied" hours
-- Charging: simulates 2 charging sessions (morning and evening)
-- RTC Drift: simulates clock drift (-500ms to +500ms) once per hour

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
INSERT INTO sensor_data (timestamp, temperature, humidity, co2, battery_voltage, battery_percent, battery_rate, battery_charging, rtc_drift_ms)
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
  -- Battery voltage: base discharge, but rises during charging
  ROUND(
    CASE
      -- Morning charge (7:00-9:00, minutes 420-540): voltage rises
      WHEN t.n BETWEEN 420 AND 540 THEN 3.9 + ((t.n - 420) / 120.0) * 0.3
      -- Evening charge (19:00-21:00, minutes 1140-1260): voltage rises
      WHEN t.n BETWEEN 1140 AND 1260 THEN 3.7 + ((t.n - 1140) / 120.0) * 0.3
      -- Normal discharge
      ELSE 4.2 - (t.n / 1440.0) * 0.5
    END + (RANDOM() % 100) / 2000.0, 3
  ) AS battery_voltage,
  -- Battery percent: rises during charging, falls otherwise
  ROUND(
    CASE
      WHEN t.n BETWEEN 420 AND 540 THEN 60.0 + ((t.n - 420) / 120.0) * 25.0
      WHEN t.n BETWEEN 1140 AND 1260 THEN 40.0 + ((t.n - 1140) / 120.0) * 30.0
      ELSE 100.0 - (t.n / 1440.0) * 60.0
    END + (RANDOM() % 100) / 100.0, 1
  ) AS battery_percent,
  -- Battery rate: positive when charging, negative when discharging
  ROUND(
    CASE
      WHEN t.n BETWEEN 420 AND 540 THEN 12.0 + (RANDOM() % 100) / 50.0
      WHEN t.n BETWEEN 1140 AND 1260 THEN 15.0 + (RANDOM() % 100) / 50.0
      ELSE -0.8 + (RANDOM() % 100) / 500.0
    END, 2
  ) AS battery_rate,
  -- Charging state: 1 during charge windows, 0 otherwise
  CASE
    WHEN t.n BETWEEN 420 AND 540 THEN 1
    WHEN t.n BETWEEN 1140 AND 1260 THEN 1
    ELSE 0
  END AS battery_charging,
  -- RTC drift: only at minute 0 of each hour (every 60 minutes), range -500ms to +500ms
  CASE
    WHEN t.n % 60 = 0 THEN (RANDOM() % 1000) - 500
    ELSE NULL
  END AS rtc_drift_ms
FROM time_series t, base_time b;
