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
    };

    // Returns a list of filled orders
    std::vector<OpenOrder> on_trade_update(int64_t trade_price) {
        std::vector<OpenOrder> filled_orders;
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
                filled_orders.push_back(*it);
                it = open_orders_.erase(it);
            } else {
                ++it;
            }
        }
        return filled_orders;
    }

    void place_order(const Order& order) {
        open_orders_.push_back({order.id, order.is_buy, order.price, order.quantity, order.origin_timestamp});
    }
    
    void cancel_all() {
        open_orders_.clear();
    }

    size_t open_order_count() const {
        return open_orders_.size();
    }

private:
    std::list<OpenOrder> open_orders_;
};

}
