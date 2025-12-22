#!/usr/bin/env python3
"""
Find specific high-drain periods in the 4.0V-4.1V range after 12/12.
"""
import json
from pathlib import Path
from datetime import datetime
from collections import defaultdict

LOGS_DIR = Path(__file__).parent.parent / "logs" / "sensor_logs"

def analyze_high_drain():
    """Find high drain periods."""
    
    # Focus on after 12/12, 4.0V-4.1V range
    cutoff_date = "20251212"
    
    high_drain_periods = []
    
    for filepath in sorted(LOGS_DIR.glob("sensor_log_*.jsonl")):
        date = filepath.stem.split('_')[-1]
        if date < cutoff_date:
            continue
        
        data = []
        with open(filepath, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line.strip())
                    data.append(entry)
                except:
                    continue
        
        prev = None
        for entry in data:
            v = entry.get('batt_voltage')
            charging = entry.get('charging', False)
            
            if v is None or charging or v < 3.0 or v > 4.3:
                prev = None
                continue
            
            try:
                dt = datetime.strptime(f"{entry['date']} {entry['time']}", "%Y.%m.%d %H:%M:%S")
            except:
                prev = None
                continue
            
            if prev is not None:
                prev_v, prev_dt, prev_entry = prev
                delta_secs = (dt - prev_dt).total_seconds()
                
                # Only 1-minute intervals
                if 50 <= delta_secs <= 70:
                    drain_mV = (prev_v - v) * 1000
                    drain_per_hour = drain_mV * 60
                    
                    # High drain in 4.0-4.1V range
                    if 4.0 <= v < 4.1 and drain_per_hour > 50:
                        high_drain_periods.append({
                            'date': date,
                            'time': entry['time'],
                            'voltage': v,
                            'prev_voltage': prev_v,
                            'drain_mV': drain_mV,
                            'drain_per_hour': drain_per_hour,
                        })
            
            prev = (v, dt, entry)
    
    print("=" * 80)
    print("HIGH DRAIN PERIODS (4.0V-4.1V range, >50 mV/h) AFTER 12/12")
    print("=" * 80)
    print()
    
    if not high_drain_periods:
        print("No high drain periods found.")
        return
    
    # Group by date
    by_date = defaultdict(list)
    for p in high_drain_periods:
        by_date[p['date']].append(p)
    
    for date in sorted(by_date.keys()):
        periods = by_date[date]
        print(f"\n📅 {date[:4]}-{date[4:6]}-{date[6:]} ({len(periods)} high-drain minutes)")
        print("-" * 60)
        
        for p in periods[:20]:  # Show first 20
            print(f"  {p['time']}: {p['prev_voltage']:.3f}V → {p['voltage']:.3f}V "
                  f"({p['drain_mV']:.1f}mV, {p['drain_per_hour']:.0f}mV/h)")
        
        if len(periods) > 20:
            print(f"  ... and {len(periods) - 20} more")
        
        # Statistics
        avg_drain = sum(p['drain_per_hour'] for p in periods) / len(periods)
        print(f"\n  Average: {avg_drain:.0f} mV/h")


def compare_daily_drain():
    """Compare daily drain rates for all log files."""
    print("\n" + "=" * 80)
    print("DAILY DRAIN RATE COMPARISON (4.0V-4.2V range only)")
    print("=" * 80)
    print()
    
    for filepath in sorted(LOGS_DIR.glob("sensor_log_*.jsonl")):
        date = filepath.stem.split('_')[-1]
        
        data = []
        with open(filepath, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line.strip())
                    data.append(entry)
                except:
                    continue
        
        drains = []
        prev = None
        for entry in data:
            v = entry.get('batt_voltage')
            charging = entry.get('charging', False)
            
            if v is None or charging or not (4.0 <= v <= 4.2):
                prev = None
                continue
            
            try:
                dt = datetime.strptime(f"{entry['date']} {entry['time']}", "%Y.%m.%d %H:%M:%S")
            except:
                prev = None
                continue
            
            if prev is not None:
                prev_v, prev_dt = prev
                delta_secs = (dt - prev_dt).total_seconds()
                
                if 50 <= delta_secs <= 70:
                    drain_mV = (prev_v - v) * 1000
                    drains.append(drain_mV)
            
            prev = (v, dt)
        
        if drains:
            avg_drain = sum(drains) / len(drains) * 60
            marker = " ⚠️" if avg_drain > 20 else ""
            print(f"{date[:4]}-{date[4:6]}-{date[6:]}: {avg_drain:>6.1f} mV/h (n={len(drains):>4}){marker}")


if __name__ == '__main__':
    analyze_high_drain()
    compare_daily_drain()

