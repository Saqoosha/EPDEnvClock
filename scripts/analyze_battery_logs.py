#!/usr/bin/env python3
"""
Analyze battery consumption from local sensor logs.
Compare battery drain rates before and after 2025-12-12.
"""
import json
import os
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass

LOGS_DIR = Path(__file__).parent.parent / "logs" / "sensor_logs"


@dataclass
class DischargeSession:
    """Represents a continuous battery discharge session."""
    start_time: datetime
    end_time: datetime
    start_voltage: float
    end_voltage: float
    duration_hours: float
    drain_rate_mV_per_hour: float
    data_points: int


def load_log_file(filepath: Path) -> List[Dict]:
    """Load a JSONL log file."""
    data = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    entry = json.loads(line)
                    data.append(entry)
                except json.JSONDecodeError:
                    continue
    return data


def parse_datetime(entry: Dict) -> datetime:
    """Parse datetime from log entry."""
    date_str = entry.get('date', '')
    time_str = entry.get('time', '')
    if date_str and time_str:
        return datetime.strptime(f"{date_str} {time_str}", "%Y.%m.%d %H:%M:%S")
    return None


def find_discharge_sessions(data: List[Dict], min_duration_hours: float = 1.0) -> List[DischargeSession]:
    """Find continuous discharge sessions (not charging, valid voltage)."""
    sessions = []
    session_data = []
    
    for entry in data:
        voltage = entry.get('batt_voltage')
        charging = entry.get('charging', False)
        dt = parse_datetime(entry)
        
        if dt is None or voltage is None or voltage < 3.0 or voltage > 4.3 or charging:
            # End current session if we have one
            if len(session_data) > 1:
                sessions.append(create_session(session_data))
            session_data = []
            continue
        
        session_data.append((dt, voltage))
    
    # Don't forget last session
    if len(session_data) > 1:
        sessions.append(create_session(session_data))
    
    # Filter by minimum duration
    sessions = [s for s in sessions if s.duration_hours >= min_duration_hours]
    
    return sessions


def create_session(data: List[Tuple[datetime, float]]) -> DischargeSession:
    """Create a DischargeSession from collected data."""
    start_dt, start_v = data[0]
    end_dt, end_v = data[-1]
    duration_hours = (end_dt - start_dt).total_seconds() / 3600
    
    if duration_hours > 0:
        drain_mV = (start_v - end_v) * 1000
        drain_rate = drain_mV / duration_hours
    else:
        drain_rate = 0
    
    return DischargeSession(
        start_time=start_dt,
        end_time=end_dt,
        start_voltage=start_v,
        end_voltage=end_v,
        duration_hours=duration_hours,
        drain_rate_mV_per_hour=drain_rate,
        data_points=len(data)
    )


def analyze_log_file(filepath: Path) -> Dict:
    """Analyze a single log file."""
    data = load_log_file(filepath)
    if not data:
        return None
    
    sessions = find_discharge_sessions(data)
    
    # Calculate average drain rate
    total_drain = 0
    total_hours = 0
    for s in sessions:
        if s.drain_rate_mV_per_hour > 0:  # Only count actual drain
            total_drain += (s.start_voltage - s.end_voltage) * 1000
            total_hours += s.duration_hours
    
    avg_drain_rate = total_drain / total_hours if total_hours > 0 else 0
    
    return {
        'file': filepath.name,
        'date': filepath.stem.split('_')[-1],
        'total_entries': len(data),
        'sessions': sessions,
        'total_hours': total_hours,
        'total_drain_mV': total_drain,
        'avg_drain_rate': avg_drain_rate
    }


def main():
    # Find all log files
    log_files = sorted(LOGS_DIR.glob("sensor_log_*.jsonl"))
    
    print("=" * 80)
    print("BATTERY CONSUMPTION ANALYSIS")
    print("=" * 80)
    
    results = []
    for filepath in log_files:
        result = analyze_log_file(filepath)
        if result:
            results.append(result)
    
    # Separate into before and after 2025-12-12
    cutoff_date = "20251212"
    before = [r for r in results if r['date'] < cutoff_date]
    after = [r for r in results if r['date'] >= cutoff_date]
    
    print(f"\n📊 Summary by Period")
    print("-" * 40)
    
    # Calculate aggregate stats
    def calc_period_stats(period_data: List[Dict], period_name: str):
        total_hours = sum(r['total_hours'] for r in period_data)
        total_drain = sum(r['total_drain_mV'] for r in period_data)
        avg_rate = total_drain / total_hours if total_hours > 0 else 0
        
        print(f"\n{period_name}:")
        print(f"  Log files: {len(period_data)}")
        print(f"  Total discharge time: {total_hours:.1f} hours")
        print(f"  Total drain: {total_drain:.0f} mV ({total_drain/1000:.3f} V)")
        print(f"  Average drain rate: {avg_rate:.2f} mV/hour")
        
        if total_hours > 0:
            # Estimate battery life (full charge is ~800mV usable range: 4.2V -> 3.4V)
            hours_full_charge = 800 / avg_rate if avg_rate > 0 else float('inf')
            print(f"  Estimated battery life: {hours_full_charge:.1f} hours ({hours_full_charge/24:.1f} days)")
        
        return avg_rate
    
    rate_before = calc_period_stats(before, "BEFORE 2025-12-12")
    rate_after = calc_period_stats(after, "AFTER 2025-12-12")
    
    if rate_before > 0 and rate_after > 0:
        change_percent = ((rate_after - rate_before) / rate_before) * 100
        print(f"\n🔍 Comparison:")
        print(f"  Drain rate change: {change_percent:+.1f}%")
        if change_percent > 0:
            print(f"  ⚠️ Battery consumption INCREASED by {change_percent:.1f}%")
        else:
            print(f"  ✅ Battery consumption decreased by {abs(change_percent):.1f}%")
    
    # Detailed per-day breakdown
    print("\n" + "=" * 80)
    print("DETAILED DAILY BREAKDOWN")
    print("=" * 80)
    print(f"{'Date':<12} {'Hours':>8} {'Drain (mV)':>12} {'Rate (mV/h)':>12} {'Sessions':>10}")
    print("-" * 58)
    
    for r in results:
        date_str = f"{r['date'][:4]}-{r['date'][4:6]}-{r['date'][6:]}"
        print(f"{date_str:<12} {r['total_hours']:>8.1f} {r['total_drain_mV']:>12.0f} {r['avg_drain_rate']:>12.2f} {len(r['sessions']):>10}")
    
    # Per-session details for recent files
    print("\n" + "=" * 80)
    print("DISCHARGE SESSIONS (After 2025-12-12)")
    print("=" * 80)
    
    for r in after:
        print(f"\n📁 {r['file']}")
        for i, s in enumerate(r['sessions'], 1):
            print(f"  Session {i}: {s.start_time.strftime('%H:%M')} - {s.end_time.strftime('%H:%M')}")
            print(f"    Duration: {s.duration_hours:.1f}h, Voltage: {s.start_voltage:.3f}V → {s.end_voltage:.3f}V")
            print(f"    Drain: {(s.start_voltage - s.end_voltage)*1000:.0f}mV, Rate: {s.drain_rate_mV_per_hour:.2f} mV/h")


if __name__ == '__main__':
    main()

