#include "feed_handler/CoinbaseLive.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionGateway.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
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
int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Allocate large buffers on the heap to prevent stack overflow (4MB each)
    auto feed_to_strategy_queue = std::make_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = std::make_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    // Enable Capture Mode
    hft::CoinbaseFeedHandler feed_handler(*feed_to_strategy_queue, true);
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue);

    execution_gateway.start();
    strategy_engine.start();
    feed_handler.start();

    // Run for 60 seconds to verify live connection
    std::cout << "Running live trading engine for 60 seconds..." << std::endl;
    for (int i = 0; i < 60 && keep_running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // std::cout << "Buffer Size: " << feed_to_strategy_queue->size() << std::endl;
    }

    std::cout << "Stopping engine..." << std::endl;
    feed_handler.stop();
    strategy_engine.stop();
    execution_gateway.stop();

    return 0;
}
