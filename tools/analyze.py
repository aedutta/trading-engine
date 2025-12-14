#!/usr/bin/env python3
import sys
import math
import csv

def print_histogram(data, bins=20):
    if not data:
        return
    
    min_val = min(data)
    max_val = max(data)
    
    # Focus histogram on the 99th percentile to avoid outliers skewing the view
    p99 = sorted(data)[int(len(data) * 0.99)]
    max_val = p99
    
    step = (max_val - min_val) / bins
    if step == 0: step = 1
    
    histogram = [0] * bins
    outliers = 0
    
    for x in data:
        if x > max_val:
            outliers += 1
            continue
        bucket = int((x - min_val) / step)
        if bucket >= bins: bucket = bins - 1
        histogram[bucket] += 1
        
    max_count = max(histogram) if histogram else 0
    scale = 50.0 / max_count if max_count > 0 else 1
    
    print(f"\nLatency Histogram (0 - {max_val:.2f} ns):")
    print("-" * 60)
    for i in range(bins):
        lower = min_val + (i * step)
        upper = min_val + ((i + 1) * step)
        count = histogram[i]
        bar = '#' * int(count * scale)
        print(f"{lower:8.1f} - {upper:8.1f} ns | {count:6d} | {bar}")
    print(f"Outliers (> {max_val:.2f} ns): {outliers}")

def main():
    filename = "latencies.csv"
    if len(sys.argv) > 1:
        filename = sys.argv[1]
        
    try:
        with open(filename, 'r') as f:
            # Skip header if present
            lines = f.readlines()
            data = []
            for line in lines:
                line = line.strip()
                if not line or not line[0].isdigit():
                    continue
                data.append(float(line))
    except FileNotFoundError:
        print(f"Error: Could not find {filename}")
        return

    if not data:
        print("No data found.")
        return

    data.sort()
    n = len(data)
    
    avg = sum(data) / n
    median = data[int(n * 0.5)]
    p99 = data[int(n * 0.99)]
    p99_9 = data[int(n * 0.999)]
    min_val = data[0]
    max_val = data[-1]
    
    print("\n" + "="*40)
    print(f"  HFT Engine Latency Report")
    print("="*40)
    print(f"  Samples : {n:,}")
    print(f"  Min     : {min_val:10.2f} ns")
    print(f"  Avg     : {avg:10.2f} ns")
    print(f"  Median  : {median:10.2f} ns")
    print(f"  99%     : {p99:10.2f} ns")
    print(f"  99.9%   : {p99_9:10.2f} ns")
    print(f"  Max     : {max_val:10.2f} ns")
    print("="*40)
    
    print_histogram(data)

if __name__ == "__main__":
    main()
