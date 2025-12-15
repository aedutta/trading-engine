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
    filename = "strategy_latencies.csv"
    title = "HFT Engine Latency Report"
    
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    if len(sys.argv) > 2:
        title = sys.argv[2]
        
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
        print(f"No data found in {filename}.")
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
    print(f"  {title}")
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

    # Strategy Performance Analysis (Only if analyzing strategy latencies)
    if "strategy" in filename:
        trades_file = "trades.csv"
        trades = []
        try:
            with open(trades_file, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    trades.append(row)
        except FileNotFoundError:
            pass

        print("\n" + "="*40)
        print(f"  Strategy Performance (Simulation)")
        print("="*40)
        
        if not trades:
            print(f"  Total Trades : 0")
            print(f"  Net PnL      : 0.00 USDT")
            print("="*40)
            return

        cash = 0.0
        position = 0.0
        volume = 0.0
        
        # Constants (assuming 1e8 scaling)
        SCALE = 1e8
        
        last_price = 0.0
        
        for t in trades:
            price = float(t['price']) / SCALE
            qty = float(t['quantity']) / SCALE
            is_buy = int(t['is_buy']) == 1
            
            last_price = price
            volume += (price * qty)
            
            if is_buy:
                position += qty
                cash -= price * qty
            else:
                position -= qty
                cash += price * qty
                
        # Mark to Market
        unrealized_pnl = position * last_price
        total_pnl = cash + unrealized_pnl
        
        # Time Analysis
        if len(trades) > 1:
            start_ts = float(trades[0]['timestamp'])
            end_ts = float(trades[-1]['timestamp'])
            # Assuming 3.0 GHz
            duration_sec = (end_ts - start_ts) / 3000000000.0
            print(f"  Duration     : {duration_sec:.2f} seconds")
            print(f"  Trades/Sec   : {len(trades)/duration_sec:.2f}")

        print(f"  Total Trades : {len(trades)}")
        print(f"  Volume       : {volume:,.2f} USDT")
        print(f"  Position     : {position:.4f} BTC")
        print(f"  Net PnL      : {total_pnl:+.4f} USDT")
        print("="*40)

if __name__ == "__main__":
    main()
