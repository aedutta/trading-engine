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
            executed_orders_.reserve(1000000);
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

        std::ofstream file("execution_latencies.csv");
        file << "latency_ns\n"; 
        for (const auto& lat : latencies_) {
            // Convert cycles to nanoseconds
            file << (lat / utils::CYCLES_PER_NS) << "\n";
        }
        file.close();

        std::ofstream trades_file("trades.csv");
        trades_file << "id,timestamp,price,quantity,is_buy\n";
        for (const auto& order : executed_orders_) {
            trades_file << order.id << "," 
                        << order.origin_timestamp << "," 
                        << order.price << "," 
                        << order.quantity << "," 
                        << (order.is_buy ? 1 : 0) << "\n";
        }
        trades_file.close();
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
                
                if (latencies_.size() < latencies_.capacity()) {
                    latencies_.push_back(latency);
                }
                
                if (executed_orders_.size() < executed_orders_.capacity()) {
                    executed_orders_.push_back(order);
                }
            } else {
                _mm_pause();
            }
        }
    }

}
