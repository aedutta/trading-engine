# High-Frequency Trading Engine (C++20)

A high-performance, low-latency trading engine simulation designed to process market data and execute strategies with sub-microsecond latency. This project demonstrates advanced C++ optimization techniques used in HFT firms, including lock-free data structures, zero-copy I/O, and kernel-level tuning.
# v2
Plans for version 2:
 - Implement more robust execution strategy (right now its just buy if less than X amt)
 - High fidelity simulation: ``FeedHandler`` pushes data as fast as the CPU allows. This is a Throughput Test, not a Strategy Simulation. A real market has time gaps between ticks.
 - Connect to real world data (crypto/coinbase)
# v1 Done
## 🛠️ Architecture

The system runs on a pipelined architecture with pinned threads to maximize cache locality:

1.  **Feed Handler (Core 1):** Streams binary market data via `mmap` and pushes to the Strategy Queue.
2.  **Strategy Engine (Core 2):** Consumes ticks, executes logic (Price < Threshold), and pushes Orders to the Execution Queue.
3.  **Execution Gateway (Core 3):** Consumes orders and measures end-to-end latency.
## 📊 Benchmarks

**Environment:** Linux (WSL2), GCC 11+, C++20
**Dataset:** 169 Million Ticks (BTC/USDT)

| Metric | Result | Notes |
| :--- | :--- | :--- |
| **Min Latency** | **~46 ns** | **< 0.05 µs** (Hardware Limit) |
| **Median Latency** | **~1.0 µs** | Queueing delay dominates |
| **Max Latency** | **~70 ms** | OS Scheduler Interrupts (WSL2) |
| **Throughput** | **~4 Million msg/s** | |

*Note: The "Max Latency" is high due to the non-realtime nature of WSL2 (Windows Subsystem for Linux). The Minimum Latency represents the true architectural speed of the code.*

## 🚀 Performance Optimizations

### 1. Zero-Copy Data Ingestion (Memory Mapped I/O)
**Technique:** Replaced `std::ifstream` and `std::vector` with `mmap` and `std::span`.
**Why:** Standard file I/O involves copying data from disk → kernel buffer → user buffer → heap memory. `mmap` maps the file directly into the process's virtual address space, allowing the OS to load pages on demand (Demand Paging) with **zero redundant copies**.
**Impact:** Reduces data loading time from minutes to microseconds and eliminates heap allocation overhead.

### 2. Binary Protocol & Fixed-Point Arithmetic
**Technique:** Pre-converted CSV text data into a packed binary format (`struct BinaryTick`) using `int64_t` for prices (Satoshis).
**Why:** 
*   **Parsing:** Parsing ASCII text (e.g., "100.50") is CPU-intensive and branch-heavy. Binary loading turns a Compute-Bound problem into a Memory-Bound one.
*   **Precision:** Floating-point math (`double`) introduces rounding errors. Fixed-point integer math is exact and often faster on ALUs.

### 3. Lock-Free Ring Buffer (SPSC)
**Technique:** Implemented a Single-Producer Single-Consumer (SPSC) queue using `std::atomic` with Acquire/Release semantics.
**Why:** `std::mutex` and locks cause thread suspension and context switches (microseconds). Atomic operations allow threads to communicate without ever sleeping.
*   **Bitwise Indexing:** Enforced buffer size to be a power of 2 to use bitwise AND (`&`) instead of modulo (`%`), saving 20-50 CPU cycles per operation.
*   **False Sharing Prevention:** Aligned `head` and `tail` indices to 64-byte cache lines (`alignas(64)`) to prevent "cache line ping-pong" between CPU cores.

### 4. CPU Pipeline Efficiency
**Technique:** Used `_mm_pause()` intrinsic instead of `std::this_thread::yield()` in busy-wait loops.
**Why:** `yield()` triggers a context switch. `_mm_pause()` keeps the thread active but hints the CPU pipeline to pause for a few cycles, reducing power consumption and improving reaction latency to nanoseconds when new data arrives.

### 5. Kernel-Level Tuning
**Technique:** Applied `madvise(MADV_SEQUENTIAL)` and `MADV_HUGEPAGE`.
**Why:** 
*   **Sequential Hint:** Triggers aggressive OS read-ahead, pre-loading data pages into RAM before the application requests them.
*   **Huge Pages:** Reduces TLB (Translation Lookaside Buffer) misses by mapping memory in 2MB chunks instead of 4KB.

### 6. Hardware-Level Micro-Optimizations (Phase 3)
**Technique:** 
*   **Cache Alignment:** Enforced `alignas(64)` on `BinaryTick` and `RingBuffer::Slot`.
*   **RDTSC Timing:** Replaced `std::chrono` with CPU Time Stamp Counter (`rdtsc`) assembly instructions.
*   **Integer Arithmetic:** Removed all floating-point division from the hot path. Prices are passed as `int64_t` (Satoshis) throughout the entire pipeline, only converting to display format at the very end. Replaced `char[]` symbol copying with `uint64_t` integer moves.
**Why:** 
*   **False Sharing:** 64-byte alignment ensures each data unit sits on its own cache line, preventing cores from invalidating each other's caches.
*   **Instruction Count:** `rdtsc` takes ~10-20 cycles vs ~200ns for system calls. Integer moves are single-cycle operations compared to `strncpy` loops.
*   **Arithmetic Latency:** Floating-point division is one of the slowest CPU instructions (~20-50 cycles). Keeping prices in Satoshis (`int64_t`) allows the strategy to use simple integer comparisons (~1 cycle), deferring expensive conversions until after the trade is executed.

---

Consumes orders and measures end-to-end latency.

## 💻 Build & Run

### Prerequisites
*   C++20 compliant compiler (GCC/Clang)
*   CMake 3.20+
*   Linux environment (for `mmap`, `sched_setaffinity`)

### Steps
1.  **Build the project:**
    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```

2.  **Convert Data (One-time setup):**
    ```bash
    # Compiles the converter tool
    g++ -O3 -std=c++20 -Iinclude tools/converter.cpp -o tools/converter
    
    # Converts CSV to Binary (Fixed-Point)
    ./tools/converter data/BTCUSDT-trades-2025-11.csv data/BTCUSDT-trades-2025-11.bin
    ```

3.  **Run the Engine:**
    ```bash
    ./build/hft_engine
    ```

4.  **Run Benchmark Suite (Automated):**
    ```bash
    ./benchmark.sh
    ```
    This script builds the project, runs the simulation, and generates a detailed latency report with a histogram.
