#!/usr/bin/env python3
"""
Analyze battery drain rate by voltage range.
Higher voltage = fuller battery, lower internal resistance.
"""
import json
from pathlib import Path
from datetime import datetime
from collections import defaultdict

LOGS_DIR = Path(__file__).parent.parent / "logs" / "sensor_logs"

def load_all_logs():
    """Load all log files."""
    all_data = []
    for filepath in sorted(LOGS_DIR.glob("sensor_log_*.jsonl")):
        date = filepath.stem.split('_')[-1]
        with open(filepath, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line.strip())
                    entry['_date'] = date
                    all_data.append(entry)
                except:
                    continue
    return all_data


def analyze_by_voltage_range(data, cutoff_date="20251212"):
    """Analyze drain rate by voltage range, before and after cutoff."""
    
    # Voltage ranges
    ranges = [
        (4.1, 4.3, "4.1V-4.3V (Full)"),
        (4.0, 4.1, "4.0V-4.1V"),
        (3.9, 4.0, "3.9V-4.0V"),
        (3.8, 3.9, "3.8V-3.9V"),
        (3.7, 3.8, "3.7V-3.8V"),
        (3.6, 3.7, "3.6V-3.7V"),
        (3.5, 3.6, "3.5V-3.6V"),
        (3.4, 3.5, "3.4V-3.5V (Low)"),
    ]
    
    # Calculate minute-to-minute drain for each period
    def calc_drains(entries, period_name):
        drains_by_range = defaultdict(list)
        
        prev = None
        for entry in entries:
            v = entry.get('batt_voltage')
            charging = entry.get('charging', False)
            dt_str = f"{entry['date']} {entry['time']}"
            
            if v is None or charging or v < 3.0 or v > 4.3:
                prev = None
                continue
            
            try:
                dt = datetime.strptime(dt_str, "%Y.%m.%d %H:%M:%S")
            except:
                prev = None
                continue
            
            if prev is not None:
                prev_v, prev_dt = prev
                delta_secs = (dt - prev_dt).total_seconds()
                
                # Only consider 1-minute intervals
                if 50 <= delta_secs <= 70:
                    drain_mV = (prev_v - v) * 1000
                    
                    # Find voltage range
                    for low, high, name in ranges:
                        if low <= v < high:
                            drains_by_range[name].append(drain_mV)
                            break
            
            prev = (v, dt)
        
        return drains_by_range
    
    # Split data
    before = [e for e in data if e['_date'] < cutoff_date]
    after = [e for e in data if e['_date'] >= cutoff_date]
    
    before_drains = calc_drains(before, "before")
    after_drains = calc_drains(after, "after")
    
    print("=" * 90)
    print("DRAIN RATE BY VOLTAGE RANGE (mV per hour)")
    print("=" * 90)
    print()
    print(f"{'Voltage Range':<20} {'BEFORE 12/12':>20} {'AFTER 12/12':>20} {'Change':>15} {'Notes':<15}")
    print("-" * 90)
    
    for low, high, name in ranges:
        before_list = before_drains.get(name, [])
        after_list = after_drains.get(name, [])
        
        # Convert per-minute to per-hour
        before_avg = sum(before_list) / len(before_list) * 60 if before_list else 0
        after_avg = sum(after_list) / len(after_list) * 60 if after_list else 0
        
        n_before = len(before_list)
        n_after = len(after_list)
        
        if before_avg > 0 and after_avg > 0:
            change = ((after_avg - before_avg) / before_avg) * 100
            change_str = f"{change:+.1f}%"
            if change > 20:
                notes = "⚠️ INCREASED"
            elif change < -20:
                notes = "✅ decreased"
            else:
                notes = ""
        else:
            change_str = "N/A"
            notes = ""
        
        before_str = f"{before_avg:.1f} (n={n_before})" if n_before else "N/A"
        after_str = f"{after_avg:.1f} (n={n_after})" if n_after else "N/A"
        
        print(f"{name:<20} {before_str:>20} {after_str:>20} {change_str:>15} {notes:<15}")
    
    print()
    print("Note: Rate is calculated from consecutive 1-minute readings, charging excluded")
    print()
    
    # Overall statistics
    print("=" * 90)
    print("OVERALL STATISTICS")
    print("=" * 90)
    
    all_before = [d for drains in before_drains.values() for d in drains]
    all_after = [d for drains in after_drains.values() for d in drains]
    
    if all_before:
        avg_before = sum(all_before) / len(all_before) * 60
        print(f"Before 12/12: {avg_before:.2f} mV/h average (n={len(all_before)} samples)")
    
    if all_after:
        avg_after = sum(all_after) / len(all_after) * 60
        print(f"After 12/12:  {avg_after:.2f} mV/h average (n={len(all_after)} samples)")
    
    if all_before and all_after:
        overall_change = ((avg_after - avg_before) / avg_before) * 100
        print(f"Overall change: {overall_change:+.1f}%")


def main():
    print("Loading all sensor logs...")
    data = load_all_logs()
    print(f"Loaded {len(data)} entries")
    print()
    
    analyze_by_voltage_range(data)


if __name__ == '__main__':
    main()

