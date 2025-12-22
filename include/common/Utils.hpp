#pragma once

#include <thread>
#include <vector>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <cstring>
#include <cstdio>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#ifdef USE_DPDK
#include <rte_cycles.h>
#endif

namespace hft::constants {
    constexpr int64_t PRICE_SCALE = 100000000; // 1e8 for Satoshis
    constexpr double PRICE_SCALE_DBL = 100000000.0;
    constexpr size_t RING_BUFFER_SIZE = 65536;
    
    // Core Pinning Configuration for AWS c7i.large (2 vCPUs)
    // vCPU 0: OS/Network Interrupts + Feed Handler + Execution Gateway
    // vCPU 1: Isolated Strategy Engine (isolcpus=1)
    constexpr int FEED_HANDLER_CORE = 0;
    constexpr int STRATEGY_ENGINE_CORE = 1;
    constexpr int EXECUTION_GATEWAY_CORE = 0;
    constexpr int LOGGER_CORE = 0;

    constexpr double DEFAULT_ORDER_QTY = 0.01;
    // Updated threshold to 110,000.00 to ensure trades trigger on current dataset (Price ~109,600)
    constexpr int64_t STRATEGY_PRICE_THRESHOLD = 11000000000000LL; 
}

namespace hft::utils {

    // cycles per nanosecond (calibrated at startup)
    inline double CYCLES_PER_NS = 3.0;

    // Function: rdtsc
    // Description: Reads the Time Stamp Counter (TSC) for high-precision timing.
    //              Uses rte_rdtsc() if DPDK is enabled, otherwise lfence; rdtsc.
    // Inputs: None.
    // Outputs: Current cycle count.
    inline uint64_t rdtsc() {
#ifdef USE_DPDK
        return rte_rdtsc();
#else
        unsigned int lo, hi;
        __asm__ volatile ("lfence; rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
#endif
    }

    // Function: cpu_relax
    // Description: Hints the CPU to pause for a few cycles in a spin-wait loop.
    inline void cpu_relax() {
#if defined(__aarch64__)
        asm volatile("yield");
#elif defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#elif defined(USE_DPDK)
        rte_pause();
#else
        // Fallback
#endif
    }

    // Function: calibrate_tsc
    // Description: Calibrates the TSC frequency against the system clock.
    inline void calibrate_tsc() {
        auto start = std::chrono::steady_clock::now();
        uint64_t start_tsc = rdtsc();
        
        // Sleep for 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto end = std::chrono::steady_clock::now();
        uint64_t end_tsc = rdtsc();
        
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        uint64_t cycles = end_tsc - start_tsc;
        
        CYCLES_PER_NS = static_cast<double>(cycles) / duration_ns;
        std::cout << "[System] Calibrated TSC Frequency: " << (CYCLES_PER_NS * 1.0) << " GHz" << std::endl;
    }

    // Function: pin_thread_to_core
    // Description: Pins the current thread to a specific CPU core.
    // Inputs: core_id - The ID of the core to pin to.
    // Outputs: None.
    inline void pin_thread_to_core(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        pthread_t current_thread = pthread_self();
        pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    }

    // Simple Latency Recorder

    // Simple Latency Recorder
    struct LatencyRecorder {
        std::vector<uint64_t> latencies;
        
        LatencyRecorder() {
            latencies.reserve(1000000);
        }

        void record(uint64_t start, uint64_t end) {
            if (end > start) {
                latencies.push_back(end - start);
            }
        }

        void save_to_csv(const std::string& filename) {
            FILE* f = fopen(filename.c_str(), "w");
            if (!f) return;
            
            for (uint64_t lat : latencies) {
                // Convert cycles to nanoseconds
                double ns = (double)lat / CYCLES_PER_NS;
                fprintf(f, "%.2f\n", ns);
            }
            fclose(f);
            std::cout << "Saved " << latencies.size() << " latency samples to " << filename << std::endl;
        }
    };
}
