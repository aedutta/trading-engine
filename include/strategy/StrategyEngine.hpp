#pragma once

#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include <atomic>
#include <thread>

namespace hft {

    // Function: StrategyEngine
    // Description: Core logic engine. Processes ticks and generates orders.
    class StrategyEngine {
    public:
        // Function: StrategyEngine
        // Description: Constructor.
        // Inputs: input_buffer - Source of ticks.
        //         output_buffer - Destination for orders.
        StrategyEngine(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& input_buffer, RingBuffer<Order, constants::RING_BUFFER_SIZE>& output_buffer);

        // Function: start
        // Description: Starts the strategy thread.
        void start();

        // Function: stop
        // Description: Stops the strategy thread.
        void stop();

    private:
        void run();

        RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& input_buffer_;
        RingBuffer<Order, constants::RING_BUFFER_SIZE>& output_buffer_;
        std::atomic<bool> running_{false};
        std::thread thread_;
    };

}
