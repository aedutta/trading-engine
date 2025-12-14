#include "feed_handler/FeedHandler.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionGateway.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <memory>

// Function: main
// Description: Entry point. Initializes components, loads data, and starts threads.
// Inputs: None.
// Outputs: Returns 0 on success.
int main() {
    // Allocate large buffers on the heap to prevent stack overflow (4MB each)
    auto feed_to_strategy_queue = std::make_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = std::make_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    hft::FeedHandler feed_handler(*feed_to_strategy_queue);
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue);

    feed_handler.init("data/BTCUSDT-trades-2025-11.bin");

    execution_gateway.start();
    strategy_engine.start();
    feed_handler.start();

    // Run for 10 seconds to collect data
    std::this_thread::sleep_for(std::chrono::seconds(3));

    feed_handler.stop();
    strategy_engine.stop();
    execution_gateway.stop();

    return 0;
}
