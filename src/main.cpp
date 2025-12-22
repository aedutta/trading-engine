#include "feed_handler/CoinbaseLive.hpp"
#include "feed_handler/CoinbaseUDP.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionGateway.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include "common/Logger.hpp"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <sys/mman.h>
#include <thread>
#include <chrono>

// Helper for Hugepage Allocation
template<typename T>
struct HugePageDeleter {
    void operator()(T* ptr) const {
        if (ptr) {
            ptr->~T();
            munmap(ptr, sizeof(T));
        }
    }
};

template<typename T>
std::unique_ptr<T, HugePageDeleter<T>> make_huge_unique() {
    // Try to allocate using Hugepages (2MB or 1GB)
    void* ptr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        std::cerr << "[System] Warning: Hugepage allocation failed. Falling back to standard pages." << std::endl;
        ptr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, 
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            throw std::bad_alloc();
        }
    } else {
        std::cout << "[System] Allocated " << (sizeof(T) / 1024 / 1024) << "MB in Hugepages." << std::endl;
    }
    
    return std::unique_ptr<T, HugePageDeleter<T>>(new (ptr) T());
}

std::atomic<bool> keep_running{true};

void signal_handler(int) {
    keep_running = false;
}

// Function: main
// Description: Entry point. Initializes components, loads data, and starts threads.
// Inputs: None.
// Outputs: Returns 0 on success.
int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start Async Logger
    hft::AsyncLogger::instance().start("hft_engine.log");
    LOG_INFO("Starting HFT Engine...");

    // Calibrate TSC for accurate timing on this specific AWS instance
    hft::utils::calibrate_tsc();

    // Allocate large buffers in Hugepages to prevent TLB misses
    auto feed_to_strategy_queue = make_huge_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = make_huge_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    // Enable Capture Mode
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue);

    execution_gateway.start();
    strategy_engine.start();

    // Use WebSocket Feed Handler (Kernel Ingest)
    hft::CoinbaseFeedHandler feed_handler(*feed_to_strategy_queue, true);
    feed_handler.start();

    // Run for specified duration (default 60s)
    int duration = 60;
    if (argc > 1) {
        // The duration is the last argument passed
        duration = std::atoi(argv[argc - 1]);
    }

    std::cout << "Running live trading engine for " << duration << " seconds..." << std::endl;
    
    // Standard Mode (WebSocket)
    for (int i = 0; i < duration && keep_running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    feed_handler.stop();

    std::cout << "Stopping engine..." << std::endl;
    LOG_INFO("Stopping engine...");
    strategy_engine.stop();
    execution_gateway.stop();
    hft::AsyncLogger::instance().stop();

    return 0;
}
