#!/bin/bash
set -e

DURATION=600 # 10 Minutes
BUILD_DIR="build"

echo "--------------------------------------------------"
echo "  HFT Hybrid Engine Benchmark (10 Minutes)"
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

echo ""
echo "--------------------------------------------------"
echo "  Benchmark Complete"
echo "--------------------------------------------------"
