#include "feed_handler/CoinbaseLive.hpp"
#include "feed_handler/CoinbaseUDP.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionGateway.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#ifdef USE_DPDK
#include "network/DPDKPoller.hpp"
#endif
#include <iostream>
#include <memory>

#include <csignal>
#include <atomic>
#include <sys/mman.h>

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

#ifdef USE_DPDK
    // Initialize DPDK EAL (Environment Abstraction Layer)
    // This must be done before any other threads are spawned
    hft::network::DPDKPoller dpdk_poller;
    int parsed_args = dpdk_poller.init(argc, argv);
    
    // Adjust argc and argv to hide EAL arguments from the rest of the application
    if (parsed_args > 0) {
        argc -= parsed_args;
        argv += parsed_args;
    }
    
    // Debug: Print remaining args
    std::cout << "[Debug] Remaining args: " << argc << std::endl;
    for(int i=0; i<argc; ++i) {
        std::cout << "argv[" << i << "]: " << argv[i] << std::endl;
    }
#endif

    // Calibrate TSC for accurate timing on this specific AWS instance
    hft::utils::calibrate_tsc();

    // Allocate large buffers in Hugepages to prevent TLB misses
    auto feed_to_strategy_queue = make_huge_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = make_huge_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    // Enable Capture Mode
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    
#ifdef USE_DPDK
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue, &dpdk_poller);
#else
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue, nullptr);
#endif

    execution_gateway.start();
    strategy_engine.start();

    // HYBRID MODE: Use WebSocket Feed Handler (Kernel Ingest) + DPDK (Ready for Egress)
    hft::CoinbaseFeedHandler feed_handler(*feed_to_strategy_queue, true);
    feed_handler.start();

    /* 
    // UDP Handler is disabled for now until we have a UDP source
    #ifdef USE_DPDK
        hft::CoinbaseUDPHandler udp_handler(*feed_to_strategy_queue);
    #endif 
    */

    // Run for specified duration (default 60s)
    int duration = 60;
    if (argc > 1) {
        // The duration is the last argument passed
        duration = std::atoi(argv[argc - 1]);
    }

    std::cout << "Running live trading engine for " << duration << " seconds..." << std::endl;
    
#ifdef USE_DPDK
    // In Hybrid Mode, we might still want to initialize DPDK for Egress (sending orders),
    // but we don't need to poll for Ingest packets on the main thread if we are using WebSockets.
    
    std::cout << "[Hybrid] DPDK Initialized. WebSocket Feed Running. Main thread waiting..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    while (keep_running) {
        // Check duration
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= duration) {
            break;
        }
        
        // Sleep to save CPU on the main thread (Feed Handler and Strategy are on pinned cores)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    // Standard Mode (WebSocket)
    for (int i = 0; i < duration && keep_running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    feed_handler.stop();
#endif

    std::cout << "Stopping engine..." << std::endl;
    strategy_engine.stop();
    execution_gateway.stop();

    return 0;
}
