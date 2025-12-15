#pragma once
#include "common/Types.hpp"
#include <vector>
#include <list>
#include <algorithm>

namespace hft {

class MatchingEngine {
public:
    struct OpenOrder {
        uint64_t id;
        bool is_buy;
        int64_t price;
        int64_t quantity;
        uint64_t timestamp;
        uint64_t live_at; // Timestamp when order becomes active (latency simulation)
    };

    struct Fill {
        uint64_t order_id;
        bool is_buy;
        int64_t price;
        int64_t quantity;
        double fee;
    };

    // Configurable Fee and Latency
    double fee_rate_ = 0.004; // 0.4%
    uint64_t latency_ns_ = 50000000; // 50ms

    // Returns a list of filled orders
    std::vector<Fill> on_trade_update(int64_t trade_price, uint64_t current_ts) {
        // 1. Move pending orders to open if latency passed
        auto p_it = pending_orders_.begin();
        while (p_it != pending_orders_.end()) {
            if (current_ts >= p_it->live_at) {
                open_orders_.push_back(*p_it);
                p_it = pending_orders_.erase(p_it);
            } else {
                ++p_it;
            }
        }

        std::vector<Fill> fills;
        auto it = open_orders_.begin();
        while (it != open_orders_.end()) {
            bool filled = false;
            // Conservative Fill Logic:
            // Buy Order at P is filled if Trade Price <= P
            // Sell Order at P is filled if Trade Price >= P
            
            if (it->is_buy && trade_price <= it->price) {
                filled = true;
            } else if (!it->is_buy && trade_price >= it->price) {
                filled = true;
            }

            if (filled) {
                // Calculate Fee (assuming price/quantity are in fixed point, we return raw fee)
                // Fee = Price * Quantity * Rate
                // Note: We need to be careful with units. 
                // If Price is 1e2 and Quantity is 1e8, P*Q is 1e10.
                // Let's just return the fee as a double relative to the notional value.
                double notional = (double)it->price * (double)it->quantity;
                double fee = notional * fee_rate_;
                
                fills.push_back({it->id, it->is_buy, it->price, it->quantity, fee});
                it = open_orders_.erase(it);
            } else {
                ++it;
            }
        }
        return fills;
    }

    void place_order(const Order& order, uint64_t current_ts) {
        pending_orders_.push_back({
            order.id, 
            order.is_buy, 
            order.price, 
            order.quantity, 
            order.origin_timestamp,
            current_ts + latency_ns_
        });
    }
    
    void cancel_all() {
        open_orders_.clear();
        pending_orders_.clear();
    }

    size_t open_order_count() const {
        return open_orders_.size() + pending_orders_.size();
    }

private:
    std::list<OpenOrder> open_orders_;
    std::list<OpenOrder> pending_orders_;
};

}
