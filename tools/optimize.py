import os
import re
import subprocess
import random
import time
import sys

# Configuration
STRATEGY_FILE = "src/strategy/StrategyEngine.cpp"
BUILD_DIR = "build"
ITERATIONS = 20

# Parameter Ranges
PARAM_BOUNDS = {
    'ALPHA_NUM': (100, 200),          # EWMA Alpha (x/1024)
    'OFI_THRESHOLD': (50000, 250000), # Signal Threshold
    'SKEW_DIVISOR': (5000, 15000),    # Impact Divisor
    'INVENTORY_SKEW': (0, 500)        # Inventory Aversion
}

def modify_strategy_params(params):
    with open(STRATEGY_FILE, 'r') as f:
        content = f.read()
    
    for key, value in params.items():
        # Regex to find "constexpr int64_t KEY = <number>;"
        # We look for the variable name, optional whitespace, equals, optional whitespace, digits, semicolon
        pattern = fr"(constexpr\s+int64_t\s+{key}\s*=\s*)(\d+)(;)"
        
        if not re.search(pattern, content):
            print(f"Warning: Could not find parameter {key} in {STRATEGY_FILE}")
            continue
            
        # Replace with new value
        content = re.sub(pattern, fr"\g<1>{int(value)}\g<3>", content)
        
    with open(STRATEGY_FILE, 'w') as f:
        f.write(content)

def compile_engine():
    try:
        # Run make in build directory
        subprocess.check_output(
            ["make", "replay_engine"], 
            cwd=BUILD_DIR, 
            stderr=subprocess.STDOUT
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Compilation failed: {e.output.decode()}")
        return False

def run_simulation():
    try:
        # Run replay engine
        subprocess.check_output(
            ["./replay_engine"], 
            cwd=BUILD_DIR, 
            stderr=subprocess.STDOUT
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Simulation failed: {e.output.decode()}")
        return False

def get_pnl():
    try:
        # Run analysis script
        output = subprocess.check_output(
            ["python3", "../tools/analyze.py", "strategy_latencies.csv"], 
            cwd=BUILD_DIR,
            stderr=subprocess.STDOUT
        ).decode()
        
        # Parse Net PnL
        # Look for "Net PnL      : +2.0147 USDT"
        match = re.search(r"Net PnL\s+:\s+([+\-]?\d+\.\d+)\s+USDT", output)
        if match:
            return float(match.group(1))
        return -9999.0
    except Exception as e:
        print(f"Analysis failed: {e}")
        return -9999.0

def main():
    print("Starting HFT Strategy Optimizer (Random Search)")
    print("=" * 60)
    
    best_pnl = -float('inf')
    best_params = {}
    
    # Initial baseline (whatever is in the file currently)
    print("Benchmarking current configuration...")
    if compile_engine() and run_simulation():
        pnl = get_pnl()
        print(f"Baseline PnL: {pnl:.4f} USDT")
        best_pnl = pnl
    
    for i in range(ITERATIONS):
        # Sample random parameters
        current_params = {
            k: random.randint(v[0], v[1]) for k, v in PARAM_BOUNDS.items()
        }
        
        print(f"\nIteration {i+1}/{ITERATIONS}")
        print(f"Testing: {current_params}")
        
        # Apply
        modify_strategy_params(current_params)
        
        # Compile & Run
        if compile_engine() and run_simulation():
            pnl = get_pnl()
            print(f"  -> PnL: {pnl:.4f} USDT")
            
            if pnl > best_pnl:
                print(f"  🚀 NEW BEST! (Previous: {best_pnl:.4f})")
                best_pnl = pnl
                best_params = current_params.copy()
        else:
            print("  -> Failed.")
            
    print("\n" + "=" * 60)
    print("OPTIMIZATION COMPLETE")
    print(f"Best PnL: {best_pnl:.4f} USDT")
    print("Best Parameters:")
    for k, v in best_params.items():
        print(f"  {k}: {v}")
        
    # Restore best parameters
    if best_params:
        print("Restoring best parameters...")
        modify_strategy_params(best_params)
        compile_engine()

if __name__ == "__main__":
    main()
