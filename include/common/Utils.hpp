#pragma once

#include <thread>
#include <vector>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <cstring>
#include <cstdio>

namespace hft::constants {
    constexpr int64_t PRICE_SCALE = 100000000; // 1e8 for Satoshis
    constexpr double PRICE_SCALE_DBL = 100000000.0;
    constexpr size_t RING_BUFFER_SIZE = 65536;
    constexpr int FEED_HANDLER_CORE = 1;
    constexpr int STRATEGY_ENGINE_CORE = 2;
    constexpr int EXECUTION_GATEWAY_CORE = 3;
    constexpr double DEFAULT_ORDER_QTY = 0.01;
    // Updated threshold to 110,000.00 to ensure trades trigger on current dataset (Price ~109,600)
    constexpr int64_t STRATEGY_PRICE_THRESHOLD = 11000000000000LL; 
}

namespace hft::utils {

    // cycles per nanosecond (approximate for 3GHz CPU)
    // In production, you would calibrate this at startup.
    constexpr double CYCLES_PER_NS = 3.0;

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

    // Function: rdtsc
    // Description: Reads the Time Stamp Counter (TSC) for high-precision timing.
    //              Uses lfence to serialize instruction execution.
    // Inputs: None.
    // Outputs: Current cycle count.
    inline uint64_t rdtsc() {
        unsigned int lo, hi;
        __asm__ volatile ("lfence; rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    }

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
