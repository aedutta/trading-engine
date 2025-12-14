#pragma once

#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include <atomic>
#include <vector>
#include <string>
#include <thread>
#include <span>

namespace hft {

    // Function: FeedHandler
    // Description: Manages market data ingestion and normalization.
    class FeedHandler {
    public:
        // Function: FeedHandler
        // Description: Constructor.
        // Inputs: output_buffer - Reference to the ring buffer for ticks.
        FeedHandler(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& output_buffer);

        ~FeedHandler();

        // Function: start
        // Description: Starts the processing thread.
        void start();

        // Function: stop
        // Description: Stops the processing thread.
        void stop();

        // Function: init
        // Description: Maps the binary market data file into memory.
        // Inputs: filename - Path to the binary file.
        void init(const std::string& filename);

    private:
        void run();

        RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& output_buffer_;
        std::atomic<bool> running_{false};
        std::thread thread_;
        
        int fd_ = -1;
        size_t file_size_ = 0;
        void* mapped_addr_ = nullptr;
        std::span<const BinaryTick> ticks_;
    };

}
