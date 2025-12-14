#include "strategy/StrategyEngine.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <immintrin.h> // For _mm_pause
#include <cstring>

namespace hft {

    StrategyEngine::StrategyEngine(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& input_buffer, RingBuffer<Order, constants::RING_BUFFER_SIZE>& output_buffer)
        : input_buffer_(input_buffer), output_buffer_(output_buffer) {}

    // Function: start
    // Description: Starts the strategy engine thread.
    // Inputs: None.
    // Outputs: None.
    void StrategyEngine::start() {
        running_ = true;
        thread_ = std::thread(&StrategyEngine::run, this);
    }

    // Function: stop
    // Description: Stops the strategy engine thread.
    // Inputs: None.
    // Outputs: None.
    void StrategyEngine::stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Function: run
    // Description: Main loop for the strategy engine. Processes ticks and generates orders.
    // Inputs: None.
    // Outputs: None.
    void StrategyEngine::run() {
        utils::pin_thread_to_core(constants::STRATEGY_ENGINE_CORE);

        BinaryTick tick;
        uint64_t order_id = 0;

        while (running_) {
            if (input_buffer_.pop(tick)) {
                // Price is now in Satoshis (int64_t).
                if (tick.price < constants::STRATEGY_PRICE_THRESHOLD) {
                    Order order;
                    order.id = ++order_id;
                    order.origin_timestamp = tick.timestamp; 
                    order.is_buy = true;
                    // Optimization: Pass fixed-point price directly, no division
                    order.price = tick.price;
                    order.quantity = constants::DEFAULT_ORDER_QTY * constants::PRICE_SCALE; // Convert default qty to fixed point
                    // Optimization: Integer copy for symbol
                    order.symbol = tick.symbol;

                    while (!output_buffer_.push(order) && running_) {
                        _mm_pause(); 
                    }
                }
            } else {
                _mm_pause(); 
            }
        }
    }

}
