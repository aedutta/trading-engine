#include "feed_handler/CoinbaseLive.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Starting Coinbase Feed Handler Test..." << std::endl;

    // Create a RingBuffer
    hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE> buffer;

    // Instantiate the FeedHandler
    hft::CoinbaseFeedHandler handler(buffer);

    // Start the handler
    handler.start();

    std::cout << "Handler started. Running for 30 seconds..." << std::endl;

    // Let it run for a while to see if it connects and receives data
    std::this_thread::sleep_for(std::chrono::seconds(30));

    std::cout << "Stopping handler..." << std::endl;
    handler.stop();

    std::cout << "Test complete." << std::endl;
    return 0;
}
