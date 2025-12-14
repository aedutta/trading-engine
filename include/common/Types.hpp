#pragma once

#include <cstdint>
#include <array>

namespace hft {

    // Function: Tick
    // Description: Represents a market data update.
    struct Tick {
        uint64_t timestamp; 
        uint64_t id;        
        double price;
        double quantity;
        bool is_bid;        
        char symbol[12];    
    };

    // Optimized binary structure for memory mapping
    // Aligned to 64 bytes to prevent false sharing and optimize cache line usage
    struct alignas(64) BinaryTick {
        uint64_t id;
        uint64_t timestamp;
        int64_t price;    // Fixed point: Satoshis (1e-8)
        int64_t quantity; // Fixed point: Satoshis (1e-8)
        uint64_t symbol;  // Encoded symbol (e.g. "BTCUSDT" as 8 bytes)
        bool is_bid;
        // Implicit padding to 64 bytes
    };

    // Function: Order
    // Description: Represents an internal order request.
    // Aligned to 64 bytes for cache efficiency
    struct alignas(64) Order {
        uint64_t id;
        uint64_t origin_timestamp; 
        int64_t price;    // Fixed point: Satoshis
        int64_t quantity; // Fixed point: Satoshis
        uint64_t symbol;  // Encoded symbol
        bool is_buy;
    };

}
