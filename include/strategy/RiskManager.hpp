#pragma once

#include "common/Types.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace hft {

    /**
     * @class RiskManager
     * @brief Handles pre-trade risk checks and position monitoring.
     * 
     * Implements strict risk limits including Max Clip, Max Notional,
     * Fat Finger protection, and a global Kill Switch.
     */
    class RiskManager {
    public:
        RiskManager() 
            : current_position_(0), 
              open_exposure_(0), 
              kill_switch_(false),
              reference_price_(0),
              balance_usd_(0),
              balance_btc_(0) {}

        /**
         * @brief Checks if an order is safe to execute based on risk limits.
         * Also optimistically decrements the balance (Shadow State).
         * 
         * @param order The order to validate.
         * @return true if the order passes all checks and funds are reserved, false otherwise.
         */
        bool check_and_reserve(const Order& order) {
            // 4. Global Kill Switch
            if (kill_switch_.load(std::memory_order_acquire)) {
                return false;
            }

            // 1. Max Clip: Quantity > 0.01 BTC
            constexpr int64_t MAX_CLIP = 1'000'000;
            if (std::abs(order.quantity) > MAX_CLIP) {
                return false;
            }

            // 2. Max Notional: Value > $5000
            constexpr __int128_t SCALING_FACTOR = 100'000'000;
            constexpr __int128_t MAX_NOTIONAL_LIMIT = static_cast<__int128_t>(5000) * SCALING_FACTOR * SCALING_FACTOR;
            
            __int128_t order_value = static_cast<__int128_t>(std::abs(order.price)) * std::abs(order.quantity);
            
            if (order_value > MAX_NOTIONAL_LIMIT) {
                return false;
            }

            // 3. Fat Finger: Price > 5% away from provided reference_price
            int64_t ref_price = reference_price_.load(std::memory_order_relaxed);
            if (ref_price > 0) {
                int64_t price_diff = std::abs(order.price - ref_price);
                if (price_diff > (ref_price / 20)) {
                    return false;
                }
            }

            // 5. Shadow Balance Check & Optimistic Decrement
            if (order.is_buy) {
                // Cost = (Price * Quantity) / 1e8
                int64_t cost = (static_cast<int64_t>(order.price) * order.quantity) / 100000000;
                
                int64_t prev = balance_usd_.fetch_sub(cost, std::memory_order_acquire);
                if (prev < cost) {
                    // Insufficient funds, rollback
                    balance_usd_.fetch_add(cost, std::memory_order_release);
                    return false;
                }
            } else {
                // Sell: Check BTC balance
                int64_t quantity = order.quantity;
                int64_t prev = balance_btc_.fetch_sub(quantity, std::memory_order_acquire);
                if (prev < quantity) {
                    // Insufficient funds, rollback
                    balance_btc_.fetch_add(quantity, std::memory_order_release);
                    return false;
                }
            }

            return true;
        }

        void rollback_order(const Order& order) {
            if (order.is_buy) {
                int64_t cost = (static_cast<int64_t>(order.price) * order.quantity) / 100000000;
                balance_usd_.fetch_add(cost, std::memory_order_release);
            } else {
                balance_btc_.fetch_add(order.quantity, std::memory_order_release);
            }
        }

        void set_balances(int64_t usd, int64_t btc) {
            balance_usd_.store(usd, std::memory_order_release);
            balance_btc_.store(btc, std::memory_order_release);
        }

        // Setters
        void set_reference_price(int64_t price) {
            reference_price_.store(price, std::memory_order_relaxed);
        }

        void set_kill_switch(bool active) {
            kill_switch_.store(active, std::memory_order_release);
        }

        void update_position(int64_t delta) {
            current_position_.fetch_add(delta, std::memory_order_relaxed);
        }

        void update_exposure(int64_t delta) {
            open_exposure_.fetch_add(delta, std::memory_order_relaxed);
        }

        // Getters
        int64_t get_position() const {
            return current_position_.load(std::memory_order_relaxed);
        }

        int64_t get_exposure() const {
            return open_exposure_.load(std::memory_order_relaxed);
        }

        bool is_kill_switch_active() const {
            return kill_switch_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<int64_t> current_position_;
        std::atomic<int64_t> open_exposure_;
        std::atomic<bool> kill_switch_;
        std::atomic<int64_t> reference_price_;
        
        // Shadow State
        std::atomic<int64_t> balance_usd_;
        std::atomic<int64_t> balance_btc_;
    };

}
