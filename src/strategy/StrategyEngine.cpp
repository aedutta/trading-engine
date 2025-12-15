#include "strategy/StrategyEngine.hpp"
#include "strategy/OrderBook.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <immintrin.h> // For _mm_pause
#include <cstring>
#include <memory>
#include <cmath>

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
        latency_recorder_.save_to_csv("strategy_latencies.csv");
    }

    // Function: run
    // Description: Main loop for the strategy engine. Processes ticks and generates orders.
    // Inputs: None.
    // Outputs: None.
    void StrategyEngine::run() {
        utils::pin_thread_to_core(constants::STRATEGY_ENGINE_CORE);

        // Lazy initialization to center around current market price
        std::unique_ptr<DenseOrderBook> order_book;

        BinaryTick tick;
        uint64_t order_id = 0;
        
        // Strategy Configuration (Integer Optimized)
        // EWMA Alpha = 0.15 (~154/1024)
        constexpr int64_t ALPHA_NUM = 174;
        constexpr int64_t ALPHA_SHIFT = 10;
        
        constexpr int64_t MAX_POSITION = 100;      // Max inventory (Lots)
        
        // Threshold in raw quantity units (Satoshis)
        // 100,000 sats = 0.001 BTC.
        constexpr int64_t OFI_THRESHOLD = 1241630; 
        
        // Skew divisor. Impact = OFI / SKEW_DIVISOR
        // If OFI = 1,000,000 (0.01 BTC), and we want 100 sats skew, Divisor = 10,000
        constexpr int64_t SKEW_DIVISOR = 13758; 

        // Inventory Skew: Price adjustment per lot of position
        constexpr int64_t INVENTORY_SKEW = 783; // 2 sats per lot 

        // Strategy State
        int64_t smoothed_ofi = 0;
        int64_t position = 0;

        while (running_) {
            if (input_buffer_.pop(tick)) {
                uint64_t start_tsc = utils::rdtsc();

                // Handle Initialization
                if (!order_book) {
                    order_book = std::make_unique<DenseOrderBook>(tick.price);
                    std::cout << "[Strategy] OrderBook initialized at price: " << tick.price << std::endl;
                }

                if (tick.is_trade) {
                    // Process Fills via Matching Engine
                    auto filled_orders = matching_engine_.on_trade_update(tick.price);
                    for (const auto& filled_order : filled_orders) {
                        position += (filled_order.is_buy ? 1 : -1);
                    }
                    latency_recorder_.record(start_tsc, utils::rdtsc());
                    continue; // Skip OFI calculation for Trade ticks
                }

                // Process Depth Update
                order_book->on_update(tick.is_bid, tick.price, tick.quantity);
                
                // 1. Alpha Calculation: Order Flow Imbalance (OFI)
                int64_t ofi = order_book->compute_ofi();
                
                // 2. Signal Smoothing (EWMA) - Integer Arithmetic
                // smoothed_t = alpha * x_t + (1-alpha) * smoothed_{t-1}
                smoothed_ofi = (ALPHA_NUM * ofi + ((1LL << ALPHA_SHIFT) - ALPHA_NUM) * smoothed_ofi) >> ALPHA_SHIFT;

                // 3. Execution Logic: Market Making with State Machine
                if (std::abs(smoothed_ofi) > OFI_THRESHOLD) {
                    bool is_buy_signal = smoothed_ofi > OFI_THRESHOLD;
                    bool is_sell_signal = smoothed_ofi < -OFI_THRESHOLD;
                    
                    bool trade_signal = false;
                    bool close_signal = false;
                    bool is_buy_order = false;

                    // State Transitions
                    if (current_state_ == State::FLAT) {
                        if (is_buy_signal) {
                            trade_signal = true;
                            is_buy_order = true;
                        } else if (is_sell_signal) {
                            trade_signal = true;
                            is_buy_order = false;
                        }
                    } else if (current_state_ == State::LONG && is_sell_signal) {
                        close_signal = true;
                        is_buy_order = false;
                    } else if (current_state_ == State::SHORT && is_buy_signal) {
                        close_signal = true;
                        is_buy_order = true;
                    }

                    if (trade_signal || close_signal) {
                        // Risk Check: Position Limits
                        if ((is_buy_order && position < MAX_POSITION) || (!is_buy_order && position > -MAX_POSITION)) {
                            
                            // Pricing Logic: Skewed Quotes
                            int64_t mid_price = order_book->get_mid_price();
                            int64_t spread = order_book->get_best_ask() - order_book->get_best_bid();
                            int64_t fair_price = mid_price + (smoothed_ofi / SKEW_DIVISOR) - (position * INVENTORY_SKEW);
                            
                            // Passive Execution: Quote at Fair Price +/- Half Spread
                            int64_t execution_price = is_buy_order ? (fair_price - spread / 2) : (fair_price + spread / 2);
                            
                            // Safety: Prevent crossing the book aggressively
                            if (is_buy_order && execution_price >= order_book->get_best_ask()) execution_price = order_book->get_best_ask() - constants::PRICE_SCALE;
                            if (!is_buy_order && execution_price <= order_book->get_best_bid()) execution_price = order_book->get_best_bid() + constants::PRICE_SCALE;

                            if (execution_price > 0) {
                                Order order;
                                order.id = ++order_id;
                                order.origin_timestamp = tick.timestamp; 
                                order.is_buy = is_buy_order;
                                order.price = execution_price;
                                order.quantity = constants::DEFAULT_ORDER_QTY * constants::PRICE_SCALE; 
                                order.symbol = tick.symbol;

                                if (output_buffer_.push(order)) {
                                    // Place order in Matching Engine for simulation
                                    matching_engine_.place_order(order);
                                    
                                    // Update State
                                    if (trade_signal) {
                                        current_state_ = is_buy_order ? State::LONG : State::SHORT;
                                    } else if (close_signal) {
                                        current_state_ = State::FLAT;
                                    }
                                }
                            }
                        }
                    }
                }
                latency_recorder_.record(start_tsc, utils::rdtsc());
            } else {
                _mm_pause(); 
            }
        }
    }

}
