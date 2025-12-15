# High-Frequency Trading Engine (C++20)

A high-performance, low-latency trading engine simulation designed to process market data and execute strategies with sub-microsecond latency. This project demonstrates advanced C++ optimization techniques used in HFT firms, including lock-free data structures, zero-copy I/O, and kernel-level tuning.

## Benchmarks (v1.1 Replay)

**Environment:** Linux (WSL2), GCC 11+, C++20
**Dataset:** Captured Coinbase L2 Data (BTC-USD)

| Metric | Result | Notes |
| :--- | :--- | :--- |
| **Min Latency** | **11.33 ns** | Pure Integer Logic |
| **Median Latency** | **13.33 ns** | Cache-Hot Path |
| **Average Latency** | **85.48 ns** | Consistent Sub-Microsecond |
| **Max Latency** | **~2.8 ms** | OS Scheduler / Cache Misses |
| **Throughput** | ~**4M** msg/s | nice |

## Trading Performance (v1.1 Simulation)

**Session:** 22 seconds of captured Coinbase L2 data (BTC-USD).

| Metric | Value |
| :--- | :--- |
| **Net PnL** | **+2.0147 USDT** |
| **Total Trades** | 157 |
| **Volume Traded** | 138,210 USDT |
| **Return on Vol** | ~1.4 bps |

## 🚀 Key Optimizations

### 1. Deterministic Replay Engine
-   **What:** Captures live Coinbase WebSocket L2 data to `market_data.bin` and replays it with nanosecond-precision timing.
-   **Why:** Allows for realistic stress-testing of the system using actual market data inter-arrival times, rather than synthetic benchmarks.

### 2. Integer-Only Strategy Logic & Arithmetic Latency
-   **Technique:** All strategy calculations (OFI Signal, EWMA Smoothing, Fair Price) use `int64_t` fixed-point arithmetic. Removed all floating-point division from the hot path.
-   **Why:** Floating-point division is one of the slowest CPU instructions (~20-50 cycles). Keeping prices in Satoshis (`int64_t`) allows the strategy to use simple integer comparisons (~1 cycle), deferring expensive conversions until after the trade is executed.

### 3. Lock-Free Ring Buffer (SPSC)
-   **Technique:** Single-Producer Single-Consumer queue using `std::atomic` with Acquire/Release semantics.
-   **Why:** Zero lock contention between threads. `std::mutex` causes context switches (microseconds), while atomics allow threads to communicate without sleeping.

### 4. Zero-Copy Data Ingestion (Memory Mapped I/O)
-   **Technique:** Replaced `std::ifstream` and `std::vector` with `mmap` and `std::span`.
-   **Why:** Standard file I/O involves copying data from disk → kernel buffer → user buffer → heap memory. `mmap` maps the file directly into the process's virtual address space, allowing the OS to load pages on demand (Demand Paging) with **zero redundant copies**.
-   **Impact:** Reduces data loading time from minutes to microseconds and eliminates heap allocation overhead.

### 5. CPU Pipeline Efficiency
-   **Technique:** Used `_mm_pause()` intrinsic instead of `std::this_thread::yield()` in busy-wait loops.
-   **Why:** `yield()` triggers a context switch. `_mm_pause()` keeps the thread active but hints the CPU pipeline to pause for a few cycles, reducing power consumption and improving reaction latency to nanoseconds when new data arrives.

### 6. Kernel-Level Tuning
-   **Technique:** Applied `madvise(MADV_SEQUENTIAL)` and `MADV_HUGEPAGE`.
-   **Why:**
    -   **Sequential Hint:** Triggers aggressive OS read-ahead, pre-loading data pages into RAM before the application requests them.
    -   **Huge Pages:** Reduces TLB (Translation Lookaside Buffer) misses by mapping memory in 2MB chunks instead of 4KB.

### 7. CPU Pinning & Isolation
-   **Technique:** Pins threads to specific CPU cores using `pthread_setaffinity_np`.
-   **Configuration (AWS c7i.large):**
    -   **Core 0:** OS Interrupts, Network I/O (Feed Handler, Execution Gateway).
    -   **Core 1:** Isolated Strategy Engine (`isolcpus=1`).
-   **Why:** Prevents the OS scheduler from preempting the critical strategy thread, ensuring deterministic execution times.

## ☁️ Production Migration (AWS)

This project is optimized for **AWS c7i.large** instances in `us-east-1`.

### Infrastructure Setup
1.  **Instance:** `c7i.large` (Intel Sapphire Rapids).
2.  **OS:** Amazon Linux 2023.
3.  **Region:** `us-east-1` (Target AZs: `use1-az4` or `use1-az6` for Coinbase).
4.  **Network:** Enable **ENA Express** on the ENI.

### Optimization Script
Run the included script to tune the OS for low latency:
```bash
sudo ./scripts/optimize_aws.sh
```
This script handles:
-   Disabling interrupt coalescing (`ethtool`).
-   Enabling busy polling (`sysctl`).
-   Verifying `isolcpus` and `chrony` status.
-   Setting CPU governor to `performance`.

### Build for Production
Compile with Sapphire Rapids optimizations:
```bash
mkdir build_prod && cd build_prod
cmake -DCMAKE_BUILD_TYPE=Production ..
make -j$(nproc)
```
-   **Why:** Maximizes L1/L2 cache hits and minimizes context switching overhead from the OS scheduler.

### 8. SIMD-Accelerated JSON Parsing
-   **Technique:** Uses `simdjson` library for parsing WebSocket messages.
-   **Why:** Parses JSON at gigabytes per second using AVX2/AVX-512 instructions, significantly reducing the "Feed Handler" latency bottleneck.

### 9. Cache-Optimized Data Structures
-   **Technique:** `DenseOrderBook` uses flat `std::vector` arrays instead of tree-based maps, and `BinaryTick` is aligned to 64 bytes (`alignas(64)`).
-   **Why:** Prevents "False Sharing" between CPU cores and ensures prefetcher-friendly memory access patterns.

## Strategy & Execution Logic

The engine implements a **Market Making** strategy driven by **Order Flow Imbalance (OFI)**.
### 1. Signal Generation (OFI)

We calculate the imbalance between Bid and Ask volume changes at the top of the book:

<div align="center">

$`\text{OFI}_t = (\mathrm{Vol}^{\mathrm{Bid}}_t - \mathrm{Vol}^{\mathrm{Bid}}_{t-1}) - (\mathrm{Vol}^{\mathrm{Ask}}_t - \mathrm{Vol}^{\mathrm{Ask}}_{t-1})`$

</div>

### 2. Signal Smoothing (EWMA)

The raw OFI is noisy, so we apply an Exponentially Weighted Moving Average using **integer-only arithmetic**:

<div align="center">

$`\text{Signal}_t = \alpha \cdot \text{OFI}_t + (1 - \alpha)\cdot \text{Signal}_{t-1}`$

</div>

*Implemented via bit-shifting (`>> 10`) to avoid floating-point latency.*

### 3. Fair Price & Execution

We quote passively around a *Fair Price* adjusted for signal strength and inventory risk:

<div align="center">

$`P_{\text{fair}} = P_{\text{mid}} + \frac{\text{Signal}_t}{\kappa} - \gamma \cdot \text{Position}`$

</div>

Where:
- $\kappa$: Signal impact divisor  
- $\gamma$: Inventory aversion parameter

## 💻 How to Run

### Prerequisites
*   C++20 compliant compiler (GCC/Clang)
*   CMake 3.20+
*   Linux environment

### 1. Build the Project
```bash
mkdir -p build && cd build
cmake ..
make replay_engine
```

### 2. Run the Simulation
The repository includes a sample `market_data.bin` (captured live data).
```bash
./replay_engine
```
*Output: Generates `strategy_latencies.csv` and `trades.csv`.*

### 3. Analyze Results
Use the Python analysis tool to generate a latency histogram and PnL report.
```bash
python3 ../tools/analyze.py strategy_latencies.csv
```
