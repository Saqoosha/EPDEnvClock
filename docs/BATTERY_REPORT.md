# 🔋 Battery Discharge Report

## Period: December 1-13, 2025 (13 days)

---

## Executive Summary

| Metric | Value |
|--------|-------|
| **Start Voltage** | 4.125V (12/1 00:00) |
| **End Voltage** | 3.375V (12/10 08:00) |
| **Total Discharge** | 750mV |
| **Total Runtime** | ~224 hours (9.3 days) |
| **Average Discharge Rate** | 3.3 mV/h |

---

## Charts

![Battery Discharge Charts](images/battery_chart.png)

📊 [Interactive Chart (HTML)](battery_chart.html)

---

## Daily Breakdown

| Date | Start V | End V | Drop | mV/h | Events |
|------|---------|-------|------|------|--------|
| 12/01 | 4.125V | 4.012V | 113mV | 4.9 | Normal operation |
| 12/02 | 4.000V | 3.906V | 94mV | 4.4 | Normal operation |
| 12/03 | 3.901V | 3.797V | 104mV | 5.0 | Normal operation |
| 12/04 | 3.794V | 3.712V | 82mV | 3.5 | Discharge slowing |
| 12/05 | 3.709V | 3.643V | 66mV | 2.9 | Discharge slowing |
| 12/06 | 3.641V | 3.589V | 52mV | 1.9 | ⚠️ Crash @ 3.626V (07:29) |
| 12/07 | 3.589V | 3.557V | 32mV | 1.6 | Slowest discharge |
| 12/08 | 3.555V | 3.499V | 56mV | 2.6 | Accelerating again |
| 12/09 | 3.496V | 3.416V | 80mV | 3.5 | Accelerating |
| 12/10 | 3.415V | 3.375V | 40mV | 5.2 | ⚠️ Brownout @ 3.375V |
| 12/12 | 3.524V | 3.415V | 109mV | 8.0 | After battery replug |
| 12/13 | 3.420V | 3.091V | 329mV | 22.4 | ⚠️ MAX17048 failed |

---

## Discharge Rate Analysis

### Key Findings

1. **Non-linear discharge curve**
   - High voltage (4.1V-3.9V): ~4.5 mV/h
   - Mid voltage (3.9V-3.7V): ~3.5 mV/h  
   - Low voltage (3.7V-3.55V): ~2.0 mV/h (minimum)
   - Very low (3.55V-3.4V): 3.0-5.0 mV/h (accelerating)

2. **Voltage vs MAX17048% accuracy**

   | Voltage | MAX17048% | Linear% | Difference |
   |---------|-----------|---------|------------|
   | 4.12V | 93% | 93% | 0% |
   | 4.00V | 78% | 83% | +5% |
   | 3.90V | 65% | 75% | +10% |
   | 3.80V | 45% | 67% | +22% |
   | 3.70V | 17% | 58% | +41% |
   | 3.60V | 4% | 50% | +46% |
   | 3.50V | 1.5% | 42% | +40% |

---

## Critical Events

### Event 1: First Crash (12/6 07:29)

- **Trigger**: Wi-Fi NTP sync at low voltage
- **Last stable voltage**: 3.626V
- **Symptom**: 5.887V spike → garbage readings → reboot loop
- **Recovery**: Battery unplug/replug

### Event 2: Brownout Cycle (12/10 ~09:00)

- **Last stable voltage**: 3.375V
- **Symptom**: Same pattern - brownout during operation
- **Status**: Continuous reboot loop at ~3.1V

### Event 3: MAX17048 Failure (12/12 10:46)

- **Trigger**: Battery replug after extended low-voltage operation
- **Symptom**: MAX17048 returned garbage values (2.7V → -1.3V → 5.1V)
- **Root cause**: I2C communication failure at low voltage
- **Recovery**: Battery unplug/replug at 11:30

### Event 4: Final Drain (12/13 00:00 - 14:47)

- **Start voltage**: 3.420V
- **End voltage**: 3.091V (crashed)
- **Duration**: 14.7 hours
- **Symptom**: MAX17048 completely unresponsive (returned 5.1V errors)
- **WiFi**: Skipped all syncs (low battery protection worked)

---

## Low-Voltage Discharge Analysis (12/13)

Detailed hourly breakdown at end-of-life voltage (3.42V → 3.09V):

| Hour | Voltage Range | Drop | Rate | Notes |
|------|---------------|------|------|-------|
| 00:00 | 3.420V → 3.412V | 8mV | 8 mV/h | WiFi skipped |
| 01:00 | 3.412V → 3.409V | 3mV | 3 mV/h | |
| 02:00 | 3.405V → 3.402V | 2mV | 2 mV/h | Minimum |
| 03:00 | 3.399V → 3.392V | 7mV | 7 mV/h | WiFi retry |
| 04:00 | 3.392V → 3.381V | 11mV | 11 mV/h | |
| 05:00 | 3.379V → 3.364V | 15mV | 15 mV/h | |
| 06:00 | 3.364V → 3.345V | 18mV | 18 mV/h | |
| 07:00 | 3.345V → 3.324V | 21mV | 21 mV/h | |
| 08:00 | 3.322V → 3.300V | 22mV | 22 mV/h | |
| 09:00 | 3.297V → 3.275V | 22mV | 22 mV/h | |
| 10:00 | 3.273V → 3.245V | 28mV | 28 mV/h | Accelerating |
| 11:00 | 3.243V → 3.214V | 28mV | 28 mV/h | |
| 12:00 | 3.211V → 3.175V | 36mV | 36 mV/h | Low voltage |
| 13:00 | 3.175V → 3.131V | 44mV | 44 mV/h | Critical |
| 14:00 | 3.130V → 3.091V | 38mV | 38 mV/h | Device limit |

**Key Finding**: Discharge rate increases exponentially below 3.3V (LiPo characteristic)

---

## Actual Battery Capacity Analysis

### Rated vs Actual Capacity

| Item | Value |
|------|-------|
| **Rated Capacity** | 1500mAh |
| **Estimated Actual** | 1200-1400mAh |
| **Efficiency** | 80-93% |

### Calculation Method

#### Method 1: Current consumption estimation

Device power profile:

- Deep sleep (53 sec/min): ~10μA
- Active (7 sec/min): ~40mA  
- Wi-Fi (10 sec/hr): ~150mA

```text
Average current ≈ 4.5mA
224 hours × 4.5mA = 1,008mAh consumed (to 3.375V)
```

#### Method 2: Linear % calculation

```text
Linear% consumed: 93.8% → 31.3% = 62.5%
If 1500mAh: 1500 × 0.625 = 937mAh in 224 hours
→ Average 4.2mA ✓ (matches estimate)
```

#### Method 3: Extrapolation to 3.0V

```text
Remaining: 3.375V → 3.0V (31.3% linear remaining)
At ~5mV/h: approximately 75 more hours
Total: 224 + 75 = 299 hours
299h × 4.5mA ≈ 1,345mAh
```

### Verdict

The battery labeled "1500mAh" has an **actual capacity of approximately 1200-1400mAh** (80-93% of rated). This is within normal range for typical LiPo batteries.

---

## Conclusions

1. **Practical minimum voltage**: ~3.4V (with Wi-Fi)
2. **Absolute minimum voltage**: ~3.09V (device cannot start)
3. **Safe operating range**: 4.2V - 3.5V
4. **MAX17048 accuracy**: Good above 3.9V, increasingly pessimistic below
5. **Linear model**: More accurate than MAX17048 below 3.8V
6. **MAX17048 I2C failure**: Occurs below ~3.1V, returns garbage values
7. **Low-voltage discharge**: Rate increases 10x below 3.3V (2mV/h → 44mV/h)

---

## Recommendations

### ✅ Implemented

1. **Low battery cutoff at 3.4V** (implemented 12/13)
   - Skip Wi-Fi/NTP sync below 3.4V
   - Prevents brownout during high-current operations

2. **Linear battery percentage for display** (implemented 12/13)
   - Formula: `(voltage - 3.4V) / (4.2V - 3.4V) * 100%`
   - 3.4V = 0%, 4.2V = 100%
   - More intuitive than MAX17048 at low voltage

3. **Voltage validation** (implemented 12/13)
   - Valid range: 2.0V - 4.4V
   - Values outside range logged as `null`
   - Display shows "ERR" for invalid readings

4. **Remove ADC fallback** (implemented 12/13)
   - ADC port was not connected, returned garbage
   - All readings now come from MAX17048 only

### 🔮 Future Improvements

1. **Enter extended sleep below 3.3V**
   - Increase sleep duration to conserve power

2. **Add MAX17048 reset on garbage values**
   - Detect I2C failure and attempt recovery

---

Report generated: 2025-12-13
