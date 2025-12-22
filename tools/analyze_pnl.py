#!/usr/bin/env python3
import sys
import csv

def main():
    filename = "trades.csv"
    if len(sys.argv) > 1:
        filename = sys.argv[1]

    if not os.path.exists(filename):
        print(f"Error: {filename} not found.")
        return

    cash = 0.0
    inventory = 0.0
    last_price = 0.0
    trades_count = 0
    volume = 0.0

    print(f"Analyzing PnL from {filename}...")
    print("-" * 60)
    print(f"{'Time':<20} | {'Side':<4} | {'Price':<10} | {'Qty':<10} | {'Cash':<15} | {'Inv':<10}")
    print("-" * 60)

    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                price = float(row['price'])
                qty = float(row['quantity'])
                is_buy = int(row['is_buy']) == 1
                timestamp = row['timestamp']

                last_price = price
                trades_count += 1
                volume += (price * qty)

                if is_buy:
                    cash -= (price * qty)
                    inventory += qty
                    side = "BUY"
                else:
                    cash += (price * qty)
                    inventory -= qty
                    side = "SELL"

                print(f"{timestamp:<20} | {side:<4} | {price:<10.2f} | {qty:<10.4f} | {cash:<15.2f} | {inventory:<10.4f}")

    except Exception as e:
        print(f"Error reading CSV: {e}")
        return

    # Mark-to-Market PnL
    unrealized_pnl = inventory * last_price
    total_pnl = cash + unrealized_pnl

    print("-" * 60)
    print(f"Total Trades: {trades_count}")
    print(f"Total Volume: ${volume:,.2f}")
    print(f"Final Inventory: {inventory:.4f} BTC")
    print(f"Final Cash: ${cash:,.2f}")
    print(f"Mark-to-Market Price: ${last_price:,.2f}")
    print(f"Unrealized PnL: ${unrealized_pnl:,.2f}")
    print("-" * 60)
    print(f"TOTAL PnL: ${total_pnl:,.2f}")
    print("-" * 60)

if __name__ == "__main__":
    import os
    main()
