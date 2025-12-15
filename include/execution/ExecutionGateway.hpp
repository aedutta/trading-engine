#pragma once

#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace hft {

    // Function: ExecutionGateway
    // Description: Handles order formatting and transmission.
    class ExecutionGateway {
    public:
        // Function: ExecutionGateway
        // Description: Constructor.
        // Inputs: input_buffer - Source of orders.
        ExecutionGateway(RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer);

        // Function: start
        // Description: Starts the execution thread.
        void start();

        // Function: stop
        // Description: Stops the execution thread.
        void stop();

    private:
        void run();

        RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer_;
        std::atomic<bool> running_{false};
        std::thread thread_;
        std::vector<uint64_t> latencies_; 
        std::vector<Order> executed_orders_;
    };

}
