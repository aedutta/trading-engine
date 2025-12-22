#!/bin/bash
set -e

DURATION=${1:-600} # Default to 600s (10 Minutes) if not provided
BUILD_DIR="build"

echo "--------------------------------------------------"
echo "  HFT Hybrid Engine Benchmark ($DURATION seconds)"
echo "--------------------------------------------------"

# 1. Build
echo "[1/4] Building with DPDK/Hybrid Mode..."
mkdir -p $BUILD_DIR
cd $BUILD_DIR
cmake -DUSE_DPDK=ON ..
make -j$(nproc)
cd ..

# 2. Run
echo "[2/4] Running Engine for $DURATION seconds..."
echo "      (This will take 10 minutes. Do not close terminal.)"
sudo ./$BUILD_DIR/hft_engine $DURATION

# 3. Analyze Strategy Latency
echo ""
echo "[3/4] Analyzing Strategy Latency (Tick-to-Signal)..."
if [ -f "strategy_latencies.csv" ]; then
    python3 tools/analyze.py strategy_latencies.csv "Strategy Latency"
else
    echo "Error: strategy_latencies.csv not found."
fi

# 4. Analyze Execution Latency
echo ""
echo "[4/4] Analyzing Execution Latency (Tick-to-Wire)..."
if [ -f "execution_latencies.csv" ]; then
    python3 tools/analyze.py execution_latencies.csv "Execution Latency"
else
    echo "Error: execution_latencies.csv not found."
fi

# 5. Analyze PnL
echo ""
echo "[5/5] Analyzing PnL..."
if [ -f "trades.csv" ]; then
    python3 tools/analyze_pnl.py trades.csv
else
    echo "Error: trades.csv not found (No trades executed?)."
fi

echo ""
echo "--------------------------------------------------"
echo "  Benchmark Complete"
echo "--------------------------------------------------"
