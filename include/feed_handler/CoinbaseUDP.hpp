#pragma once

#include "common/Types.hpp"
#include "common/RingBuffer.hpp"
#include <iostream>
#include <cstdint>

namespace hft {

    class CoinbaseUDPHandler {
    public:
        CoinbaseUDPHandler(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& buffer)
            : buffer_(buffer) {}

        // This method is called by the DPDK Poller when a packet arrives
        void on_packet(const uint8_t* data, uint16_t len) {
            // TODO: Implement SBE (Simple Binary Encoding) Parser here
            // For now, we just simulate a tick extraction
            
            // 1. Parse SBE Header
            // 2. Extract Price/Qty
            // 3. Push to RingBuffer
            
            // Placeholder:
            // BinaryTick tick;
            // tick.price = ...
            // tick.quantity = ...
            // buffer_.push(tick);
        }

    private:
        RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& buffer_;
    };

}
