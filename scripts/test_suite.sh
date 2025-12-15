#!/bin/bash
set -e

# Default values
MODE="sim"
TEST_TYPE="all"
DURATION=10

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    sim|prod)
      MODE="$1"
      shift
      ;;
    unit|integration|benchmark|all)
      TEST_TYPE="$1"
      shift
      ;;
    --duration)
      DURATION="$2"
      shift
      shift
      ;;
    *)
      echo "Unknown argument: $1"
      echo "Usage: ./scripts/test_suite.sh [sim|prod] [unit|integration|benchmark|all] [--duration SECONDS]"
      exit 1
      ;;
  esac
done

echo "========================================"
echo "  HFT Engine Test Suite"
echo "  Mode:     $MODE"
echo "  Type:     $TEST_TYPE"
echo "  Duration: ${DURATION}s"
echo "========================================"

BUILD_DIR="build_test_$MODE"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure CMake
CMAKE_FLAGS=""
if [ "$MODE" == "prod" ]; then
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Production -DENABLE_DPDK=ON"
else
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Debug -DENABLE_DPDK=OFF"
fi

echo "[1/3] Configuring CMake..."
cmake $CMAKE_FLAGS .. > /dev/null

echo "[2/3] Building..."
make -j$(nproc) > /dev/null

cd ..

# Function to run integration tests
run_integration() {
    echo "----------------------------------------"
    echo "  Running Integration Tests"
    echo "----------------------------------------"
    ./$BUILD_DIR/integration_feed $DURATION
}

# Function to run benchmarks
run_benchmark() {
    echo "----------------------------------------"
    echo "  Running Benchmarks"
    echo "----------------------------------------"
    
    # Pass DPDK args if in prod mode, but the duration is passed as the first non-EAL arg
    # Wait, DPDK EAL args consume argv until --
    # My main.cpp expects duration as argv[1].
    # If I use DPDK, I need to pass EAL args first, then --, then my args.
    
    if [ "$MODE" == "prod" ]; then
        if [ "$EUID" -ne 0 ]; then 
            echo "Error: Production benchmark requires root (sudo)."
            exit 1
        fi
        # Setup hugepages if needed
        echo 256 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || true
        modprobe vfio-pci 2>/dev/null || true
        
        # EAL args: -l 0-1 --proc-type=auto
        # App args: duration
        ./$BUILD_DIR/hft_engine -l 0-1 --proc-type=auto -- $DURATION
    else
        ./$BUILD_DIR/hft_engine $DURATION
    fi
    
    # Analyze
    echo "Analyzing results..."
    # Fix permissions if root
    if [ "$EUID" -eq 0 ]; then
        chown $SUDO_USER:$SUDO_USER *.csv 2>/dev/null || true
    fi
    
    python3 tools/analyze.py strategy_latencies.csv "Strategy Logic Latency"
    if [ -f execution_latencies.csv ]; then
        python3 tools/analyze.py execution_latencies.csv "Execution Latency"
    fi
}

# Execute requested tests
if [ "$TEST_TYPE" == "integration" ] || [ "$TEST_TYPE" == "all" ]; then
    run_integration
fi

if [ "$TEST_TYPE" == "benchmark" ] || [ "$TEST_TYPE" == "all" ]; then
    run_benchmark
fi

echo "========================================"
echo "  Test Suite Completed Successfully"
echo "========================================"
