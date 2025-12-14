#!/bin/bash
set -e

echo "--------------------------------------------------"
echo "  HFT Engine Benchmark Suite"
echo "--------------------------------------------------"

# 1. Build
echo "[1/3] Building..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release .. > /dev/null
make -j$(nproc) > /dev/null
cd ..

# 2. Run
echo "[2/3] Running Simulation (5 seconds)..."
./build/hft_engine

# 3. Analyze
echo "[3/3] Analyzing Results..."
python3 tools/analyze.py latencies.csv
