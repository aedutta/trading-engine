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
#endif

    // Calibrate TSC for accurate timing on this specific AWS instance
    hft::utils::calibrate_tsc();

    // Allocate large buffers on the heap to prevent stack overflow (4MB each)
    auto feed_to_strategy_queue = std::make_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = std::make_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    // Enable Capture Mode
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue);

    execution_gateway.start();
    strategy_engine.start();

#ifndef USE_DPDK
    // Standard Mode: Use WebSocket Feed Handler
    hft::CoinbaseFeedHandler feed_handler(*feed_to_strategy_queue, true);
    feed_handler.start();
#else
    // DPDK Mode: Use UDP Feed Handler (Driven by Poller)
    hft::CoinbaseUDPHandler udp_handler(*feed_to_strategy_queue);
#endif

    // Run for specified duration (default 60s)
    int duration = 60;
    if (argc > 1) {
        duration = std::atoi(argv[1]);
    }

    std::cout << "Running live trading engine for " << duration << " seconds..." << std::endl;
    
#ifdef USE_DPDK
    // In DPDK mode, the main thread becomes the poller
    // We poll for packets and pass them to the strategy
    // Note: In a real HFT system, we would have a dedicated core for this.
    // Here we use the main thread for simplicity in this demo.
    
    std::cout << "[DPDK] Starting Polling Loop on Main Thread..." << std::endl;
    dpdk_poller.start();
    
    auto start_time = std::chrono::steady_clock::now();
    while (keep_running) {
        // Check duration
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= duration) {
            break;
        }
        
        dpdk_poller.poll([&](const uint8_t* data, uint16_t len) {
            // Pass UDP payload to the UDP Handler
            udp_handler.on_packet(data, len);
        });
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
