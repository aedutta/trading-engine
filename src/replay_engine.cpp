#include "feed_handler/CoinbaseLive.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionGateway.hpp"
#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <thread>

// Replay Engine
// Reads market_data.bin and feeds the engine with precise timing.

struct RecordedMessage {
    uint64_t timestamp;
    std::string data;
};

int main() {
    std::cout << "Loading market_data.bin..." << std::endl;
    std::ifstream file("market_data.bin", std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open market_data.bin" << std::endl;
        return 1;
    }

    std::vector<RecordedMessage> messages;
    messages.reserve(100000);

    while (file.peek() != EOF) {
        uint64_t ts;
        uint32_t len;
        file.read(reinterpret_cast<char*>(&ts), sizeof(ts));
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        
        if (file.gcount() != sizeof(len)) break;

        std::string data(len, '\0');
        file.read(&data[0], len);
        messages.push_back({ts, std::move(data)});
    }
    std::cout << "Loaded " << messages.size() << " messages." << std::endl;

    if (messages.empty()) return 0;

    // Setup Engine
    auto feed_to_strategy_queue = std::make_unique<hft::RingBuffer<hft::BinaryTick, hft::constants::RING_BUFFER_SIZE>>();
    auto strategy_to_exec_queue = std::make_unique<hft::RingBuffer<hft::Order, hft::constants::RING_BUFFER_SIZE>>();

    // Note: We don't call start() on feed_handler because we don't want the WebSocket thread.
    // We only use it for parsing.
    hft::CoinbaseFeedHandler feed_handler(*feed_to_strategy_queue, false); 
    hft::StrategyEngine strategy_engine(*feed_to_strategy_queue, *strategy_to_exec_queue);
    hft::ExecutionGateway execution_gateway(*strategy_to_exec_queue);

    execution_gateway.start();
    strategy_engine.start();

    std::cout << "Starting Replay..." << std::endl;
    
    // Pin Replay Thread
    hft::utils::pin_thread_to_core(hft::constants::FEED_HANDLER_CORE);

    uint64_t start_tsc = hft::utils::rdtsc();
    uint64_t first_msg_ts = messages[0].timestamp;

    for (const auto& msg : messages) {
        // Calculate target time
        uint64_t target_delta = msg.timestamp - first_msg_ts;
        
        // Spin wait
        while (true) {
            uint64_t current_delta = hft::utils::rdtsc() - start_tsc;
            if (current_delta >= target_delta) break;
            _mm_pause();
        }

        // Push
        feed_handler.process_message(msg.data);
    }

    std::cout << "Replay Complete." << std::endl;
    
    // Allow strategy to finish processing
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    strategy_engine.stop();
    execution_gateway.stop();

    return 0;
}
