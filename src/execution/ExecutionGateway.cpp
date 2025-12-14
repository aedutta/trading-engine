#include "execution/ExecutionGateway.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <chrono>
#include <fstream>
#include <immintrin.h> // For _mm_pause

namespace hft {

    ExecutionGateway::ExecutionGateway(RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer)
        : input_buffer_(input_buffer) {
            latencies_.reserve(1000000); 
        }

    // Function: start
    // Description: Starts the execution gateway thread.
    // Inputs: None.
    // Outputs: None.
    void ExecutionGateway::start() {
        running_ = true;
        thread_ = std::thread(&ExecutionGateway::run, this);
    }

    // Function: stop
    // Description: Stops the execution gateway thread and dumps latency data.
    // Inputs: None.
    // Outputs: None.
    void ExecutionGateway::stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }

        std::ofstream file("latencies.csv");
        file << "latency_ns\n"; 
        for (const auto& lat : latencies_) {
            // Convert cycles to nanoseconds
            file << (lat / utils::CYCLES_PER_NS) << "\n";
        }
        file.close();
    }

    // Function: run
    // Description: Main loop for the execution gateway. Processes orders and measures latency.
    // Inputs: None.
    // Outputs: None.
    void ExecutionGateway::run() {
        utils::pin_thread_to_core(constants::EXECUTION_GATEWAY_CORE);

        Order order;

        while (running_) {
            if (input_buffer_.pop(order)) {
                uint64_t now = utils::rdtsc();
                // Note: order.origin_timestamp must also be captured via rdtsc() in FeedHandler/Strategy
                // Currently it is likely still using chrono. We need to update FeedHandler/Strategy too.
                uint64_t latency = now - order.origin_timestamp;
                latencies_.push_back(latency);
            } else {
                _mm_pause();
            }
        }
    }

}
