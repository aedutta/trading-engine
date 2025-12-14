#pragma once
#include <vector>
#include <cstdint>
#include <limits>

namespace hft {
    // A simplified "Dense" Order Book optimized for cache locality.
    // It only tracks the aggregate volume at each price level.
    class DenseOrderBook {
        static constexpr int64_t BOOK_SIZE = 100000; 
        static constexpr int64_t CENTER_INDEX = BOOK_SIZE / 2;
        static constexpr int64_t TICK_SIZE = 1000000; // 0.01 USDT in Satoshis

        struct Level {
            int64_t quantity = 0; 
            int64_t order_count = 0;
        };

        std::vector<Level> bids_;
        std::vector<Level> asks_;
        
        int64_t center_price_ = 0;
        int64_t min_active_idx_ = std::numeric_limits<int64_t>::max();
        int64_t max_active_idx_ = std::numeric_limits<int64_t>::min();

    public:
        DenseOrderBook(int64_t initial_price) : bids_(BOOK_SIZE), asks_(BOOK_SIZE), center_price_(initial_price) {}

        void on_update(bool is_bid, int64_t price, int64_t quantity) {
            int64_t delta = price - center_price_;
            int64_t index = CENTER_INDEX + (delta / TICK_SIZE);

            if (index < 0 || index >= BOOK_SIZE) return; 

            std::vector<Level>& book = is_bid ? bids_ : asks_;
            book[index].quantity = quantity; 

            if (quantity > 0) {
                if (index < min_active_idx_) min_active_idx_ = index;
                if (index > max_active_idx_) max_active_idx_ = index;
            }
        }
        
        int64_t get_best_bid() const {
            // Scan downwards from max active index
            for (int64_t i = max_active_idx_; i >= 0; --i) {
                if (bids_[i].quantity > 0) return center_price_ + (i - CENTER_INDEX) * TICK_SIZE;
            }
            return 0;
        }
    };
}