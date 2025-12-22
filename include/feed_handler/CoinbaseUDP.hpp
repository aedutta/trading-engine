#pragma once

#include "common/Types.hpp"
#include "common/RingBuffer.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <cstdint>
#include <cstring>

// Compiler hint for branch prediction
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

namespace hft {

    // ==========================================================
    // 1. Define Wire Protocol (SBE Layout)
    // ==========================================================
    #pragma pack(push, 1)

    struct SbeHeader {
        uint16_t block_length;
        uint16_t template_id;
        uint16_t schema_id;
        uint16_t version;
    };

    // Example Message: MDIncrementalRefreshBook (ID: 202)
    struct MDUpdateHeader {
        uint64_t transact_time;
        uint32_t match_event_indicator;
        // ... other fixed fields ...
    };

    struct MDEntry {
        int64_t price_mantissa;
        uint32_t order_qty;
        int8_t  price_exponent; 
        uint8_t side; // 0=Buy, 1=Sell
        // ... padding or other fields
    };

    #pragma pack(pop)

    // ==========================================================
    // 2. Hot Path Handler
    // ==========================================================

    class CoinbaseUDPHandler {
    public:
        // Use an initialization list for references
        explicit CoinbaseUDPHandler(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& buffer)
            : buffer_(buffer) {}

        // Mark as always_inline to ensure the compiler embeds this in the DPDK polling loop
        __attribute__((always_inline))
        void on_packet(const uint8_t* data, uint16_t len) {
            
            // 1. Bounds Check (Cheap sanity check)
            if (unlikely(len < sizeof(SbeHeader))) return;

            // 2. Cast Header (Zero-Copy)
            // In HFT, we trust the pointer cast. 
            // 'data' is the raw memory address from the NIC.
            const auto* header = reinterpret_cast<const SbeHeader*>(data);

            // 3. Filter by Message Type (Template ID)
            // Example: We only care about Book Updates (ID 202), ignore Heartbeats (ID 0)
            switch (header->template_id) {
                case 202: // MDIncrementalRefresh
                    process_book_update(data, len, header->block_length);
                    break;
                
                case 201: // MDSnapshot (Handle heavy logic elsewhere)
                case 0:   // Heartbeat
                default:
                    break;
            }
        }

    private:
        RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& buffer_;

        // Force inline this specific handler too
        __attribute__((always_inline))
        void process_book_update(const uint8_t* base_ptr, uint16_t len, uint16_t block_len) {
            (void)len; // Suppress unused parameter warning
            // SBE format: Header -> RootBlock -> RepeatingGroup

            // Pointer Arithmetic: Skip Header
            const uint8_t* ptr = base_ptr + sizeof(SbeHeader);

            // Cast the Root Block (Fixed fields)
            // const auto* root = reinterpret_cast<const MDUpdateHeader*>(ptr);
            
            // Optimization: If we only need price/size, we can calculate the offset 
            // to the repeating group directly if the schema is fixed version.
            
            // [Pseudo-logic for skipping to entries]
            ptr += block_len; 

            // Assuming first byte after root block is 'NumInGroup' (SBE standard)
            // SBE Group Header: blockLength (uint16) + numInGroup (uint8 or uint16)
            // *NOTE: Check your specific schema.xml for group header format*
            
            // Simple Parsing Example:
            // SbeGroupHeader* group = (SbeGroupHeader*)ptr;
            // ptr += sizeof(SbeGroupHeader);
            
            // Fake extraction for demo:
            const auto* entry = reinterpret_cast<const MDEntry*>(ptr);

            // 4. Construct internal BinaryTick
            BinaryTick tick;
            
            // 5. Normalization
            tick.price = entry->price_mantissa; 
            tick.quantity = entry->order_qty;
            tick.symbol = 0x42544355534454; // "BTCUSDT" in hex
            
            // Map Side: 0=Buy (Bid), 1=Sell (Ask)
            tick.is_bid = (entry->side == 0);
            tick.is_trade = false;    // This is a book update
            tick.is_snapshot = false; // Incremental update
            
            // Use transaction time from the root block (we need to pass it down or just use 0 for now)
            // For this hot-path demo, we'll skip passing the root header down to save registers
            tick.timestamp = 0; 
            tick.id = 0;

            // 6. Lock-Free Push
            // Using `write` if your ringbuffer supports zero-copy writing is better
            // buffer_.write([&](BinaryTick& t) { t = tick; });
            buffer_.push(tick);
        }
    };
}
