#pragma once
#include <vector>
#include <cstdint>
#include <limits>

namespace hft {
    // A simplified "Dense" Order Book optimized for cache locality.
    // It only tracks the aggregate volume at each price level.
    class DenseOrderBook {
        static constexpr int64_t BOOK_SIZE = 1000000; // Increased to 1M to cover +/- 5000 USDT
        static constexpr int64_t CENTER_INDEX = BOOK_SIZE / 2;
        static constexpr int64_t TICK_SIZE = 1000000; // 0.01 USDT in Satoshis

        struct Level {
            int64_t quantity = 0; 
            int64_t order_count = 0;
        };

        std::vector<Level> bids_;
        std::vector<Level> asks_;
        
        int64_t center_price_ = 0;
        int64_t best_bid_idx_ = -1;
        int64_t best_ask_idx_ = BOOK_SIZE;

        // OFI State
        struct BookState {
            int64_t bid_price = 0;
            int64_t bid_qty = 0;
            int64_t ask_price = 0;
            int64_t ask_qty = 0;
        };
        BookState prev_state_;

        // Bitmask for O(1) navigation
        // 100,000 levels / 64 bits per chunk = 1563 chunks
        std::vector<uint64_t> bid_masks_;
        std::vector<uint64_t> ask_masks_;

    public:
        DenseOrderBook(int64_t initial_price) 
            : bids_(BOOK_SIZE), asks_(BOOK_SIZE), 
              center_price_(initial_price),
              bid_masks_((BOOK_SIZE + 63) / 64, 0),
              ask_masks_((BOOK_SIZE + 63) / 64, 0) {}

        void on_update(bool is_bid, int64_t price, int64_t quantity) {
            int64_t delta = price - center_price_;
            int64_t index = CENTER_INDEX + (delta / TICK_SIZE);

            if (index < 0 || index >= BOOK_SIZE) [[unlikely]] return; 

            std::vector<Level>& book = is_bid ? bids_ : asks_;
            book[index].quantity = quantity; 
            
            // Update Bitmask
            std::vector<uint64_t>& masks = is_bid ? bid_masks_ : ask_masks_;
            uint64_t chunk_idx = index / 64;
            uint64_t bit_idx = index % 64;

            if (quantity > 0) {
                masks[chunk_idx] |= (1ULL << bit_idx);
            } else {
                masks[chunk_idx] &= ~(1ULL << bit_idx);
            }

            // Update Best Indices using Bitmasks (O(1) search)
            if (is_bid) {
                if (quantity > 0) {
                    if (index > best_bid_idx_) best_bid_idx_ = index;
                } else if (index == best_bid_idx_) {
                    // Find next best bid
                    while (best_bid_idx_ >= 0) {
                        uint64_t chunk = best_bid_idx_ / 64;
                        uint64_t mask = bid_masks_[chunk];
                        
                        // Mask out bits higher than current index in this chunk
                        uint64_t relevant_bits = mask & ((1ULL << ((best_bid_idx_ % 64) + 1)) - 1);
                        
                        if (relevant_bits) {
                            // Found a bit in this chunk
                            best_bid_idx_ = (chunk * 64) + (63 - __builtin_clzll(relevant_bits));
                            return;
                        }
                        
                        // Move to previous chunk
                        best_bid_idx_ = (chunk * 64) - 1;
                    }
                }
            } else {
                // Ask logic
                if (quantity > 0) {
                    if (index < best_ask_idx_) best_ask_idx_ = index;
                } else if (index == best_ask_idx_) {
                    // Find next best ask
                    while (best_ask_idx_ < BOOK_SIZE) {
                        uint64_t chunk = best_ask_idx_ / 64;
                        uint64_t mask = ask_masks_[chunk];
                        
                        // Mask out bits lower than current index in this chunk
                        uint64_t relevant_bits = mask & ~((1ULL << (best_ask_idx_ % 64)) - 1);
                        
                        if (relevant_bits) {
                            // Found a bit in this chunk
                            best_ask_idx_ = (chunk * 64) + __builtin_ctzll(relevant_bits);
                            return;
                        }
                        
                        // Move to next chunk
                        best_ask_idx_ = (chunk + 1) * 64;
                    }
                }
            }
        }
        
        int64_t compute_ofi() {
            int64_t current_bid_price = get_best_bid();
            int64_t current_bid_qty = (best_bid_idx_ >= 0) ? bids_[best_bid_idx_].quantity : 0;
            
            int64_t current_ask_price = get_best_ask();
            int64_t current_ask_qty = (best_ask_idx_ < BOOK_SIZE) ? asks_[best_ask_idx_].quantity : 0;

            int64_t e_b = 0;
            if (current_bid_price > prev_state_.bid_price) {
                e_b = current_bid_qty;
            } else if (current_bid_price == prev_state_.bid_price) {
                e_b = current_bid_qty - prev_state_.bid_qty;
            } else {
                e_b = -prev_state_.bid_qty;
            }

            int64_t e_a = 0;
            if (current_ask_price < prev_state_.ask_price) { // Lower ask is better (price improvement)
                e_a = current_ask_qty;
            } else if (current_ask_price == prev_state_.ask_price) {
                e_a = current_ask_qty - prev_state_.ask_qty;
            } else {
                e_a = -prev_state_.ask_qty;
            }

            // Update state
            prev_state_ = {current_bid_price, current_bid_qty, current_ask_price, current_ask_qty};
            
            // OFI = e_b - e_a
            return e_b - e_a;
        }

        int64_t get_best_bid() const {
            if (best_bid_idx_ >= 0) {
                return center_price_ + (best_bid_idx_ - CENTER_INDEX) * TICK_SIZE;
            }
            return 0;
        }

        int64_t get_best_ask() const {
            if (best_ask_idx_ < BOOK_SIZE) {
                return center_price_ + (best_ask_idx_ - CENTER_INDEX) * TICK_SIZE;
            }
            return 0;
        }

        int64_t get_mid_price() const {
             if (best_bid_idx_ < 0 || best_ask_idx_ >= BOOK_SIZE) return center_price_;
             int64_t bid_px = center_price_ + (best_bid_idx_ - CENTER_INDEX) * TICK_SIZE;
             int64_t ask_px = center_price_ + (best_ask_idx_ - CENTER_INDEX) * TICK_SIZE;
             return (bid_px + ask_px) / 2;
        }

        double compute_imbalance(int depth) const {
            double bid_pressure = 0;
            double ask_pressure = 0;
            
            // Scan Bids
            int count = 0;
            for (int64_t i = best_bid_idx_; i >= 0 && count < depth; --i) {
                if (bids_[i].quantity > 0) {
                    double weight = 1.0 / (count + 1);
                    bid_pressure += bids_[i].quantity * weight;
                    count++;
                }
            }
            
            // Scan Asks
            count = 0;
            for (int64_t i = best_ask_idx_; i < BOOK_SIZE && count < depth; ++i) {
                if (asks_[i].quantity > 0) {
                    double weight = 1.0 / (count + 1);
                    ask_pressure += asks_[i].quantity * weight;
                    count++;
                }
            }
            
            return (bid_pressure - ask_pressure) / (bid_pressure + ask_pressure + 1e-9);
        }
    };
}