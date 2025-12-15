#pragma once

// Common utilities for ring buffer and timing
#include "../common/RingBuffer.hpp"
#include "../common/Types.hpp"
#include "../common/Utils.hpp"

// Parsing and Networking
#include "simdjson.h"
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>

// Standard Library
#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring> // For optimization (memcmp)
#include <fstream>

namespace hft {

    class CoinbaseFeedHandler {
        // Core output buffer to the strategy engine
        RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& output_buffer_;
        
        // Thread management
        std::atomic<bool> running_{false};
        
        // Parsing state
        // simdjson::ondemand::parser parser_; // Removed: using thread_local parser
        
        // Logic state
        bool synchronized_ = false;       // Have we processed the snapshot?
        int64_t last_sequence_num_ = -1;  // For gap detection
        
        // Networking handles
        ix::WebSocket webSocket_;

        // Capture
        std::ofstream capture_file_;
        bool capture_enabled_ = false;

    public:
        // Constructor injection of the RingBuffer dependency
        CoinbaseFeedHandler(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& buffer, bool capture = false) 
            : output_buffer_(buffer), capture_enabled_(capture) {
            // Initialize network system (required for Windows, harmless on Linux)
            ix::initNetSystem();
            if (capture_enabled_) {
                capture_file_.open("market_data.bin", std::ios::binary);
            }
        }

        ~CoinbaseFeedHandler() {
            if (capture_file_.is_open()) capture_file_.close();
            stop();
            ix::uninitNetSystem();
        }

        // Lifecycle management: Start the network thread
        void start() {
            running_ = true;
            
            std::string url("wss://advanced-trade-ws.coinbase.com");
            webSocket_.setUrl(url);
            
            // Optional: Heartbeat (Ping) every 15 seconds to keep connection alive
            webSocket_.setPingInterval(15);

            // Setup callback
            webSocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
                // Thread Pinning (Run once per thread)
                static thread_local bool pinned = false;
                if (!pinned) {
                    utils::pin_thread_to_core(constants::FEED_HANDLER_CORE);
                    pinned = true;
                }

                if (msg->type == ix::WebSocketMessageType::Message) {
                    if (this->capture_enabled_ && this->capture_file_.is_open()) {
                        uint64_t ts = utils::rdtsc();
                        uint32_t len = static_cast<uint32_t>(msg->str.size());
                        this->capture_file_.write(reinterpret_cast<const char*>(&ts), sizeof(ts));
                        this->capture_file_.write(reinterpret_cast<const char*>(&len), sizeof(len));
                        this->capture_file_.write(msg->str.data(), len);
                    }
                    this->process_message(msg->str);
                } else if (msg->type == ix::WebSocketMessageType::Open) {
                    std::cout << "[Coinbase] Connected. Subscribing..." << std::endl;
                    this->subscribe();
                } else if (msg->type == ix::WebSocketMessageType::Close) {
                    std::cout << "[Coinbase] Disconnected. Code: " << msg->closeInfo.code 
                              << " Reason: " << msg->closeInfo.reason << std::endl;
                    this->synchronized_ = false;
                    this->last_sequence_num_ = -1;
                } else if (msg->type == ix::WebSocketMessageType::Error) {
                    std::cout << "[Coinbase] Error: " << msg->errorInfo.reason << std::endl;
                }
            });

            // Start connection
            webSocket_.start();
        }

        // Lifecycle management: Graceful shutdown
        void stop() {
            running_ = false;
            webSocket_.stop();
        }

        // Core Parsing Logic (Exposed for Replay/Testing)
        void process_message(std::string_view message) {
            // Optimization: Reuse a thread-local buffer to avoid allocation
            static thread_local std::vector<char> buffer;
            // Optimization: Thread-local parser to avoid race conditions
            static thread_local simdjson::dom::parser parser;

            if (buffer.size() < message.size() + simdjson::SIMDJSON_PADDING) {
                buffer.resize(message.size() + simdjson::SIMDJSON_PADDING);
            }
            std::memcpy(buffer.data(), message.data(), message.size());
            // Add padding (simdjson requirement)
            std::memset(buffer.data() + message.size(), 0, simdjson::SIMDJSON_PADDING);

            simdjson::dom::element doc;
            
            // 1. Parse Document
            auto error = parser.parse(buffer.data(), message.size(), false).get(doc);
            if (error) {
                std::cerr << "[Coinbase] JSON Parse Error: " << simdjson::error_message(error) << std::endl;
                return; 
            }

            // 2. Extract Channel Name
            std::string_view channel;
            if (doc["channel"].get(channel) != simdjson::SUCCESS) return;

            // Global Sequence Number Handling
            int64_t current_seq;
            if (doc["sequence_num"].get(current_seq) == simdjson::SUCCESS) {
                if (last_sequence_num_ != -1 && current_seq != last_sequence_num_ + 1) {
                    // GAP DETECTED!
                    std::cerr << "[Coinbase] Gap detected: " << last_sequence_num_ << " -> " << current_seq << std::endl;
                    // Force reconnect to resync
                    webSocket_.close();
                    return; 
                }
                last_sequence_num_ = current_seq;
            }

            // 3. Heartbeat Handling
            if (channel == "heartbeats") {
                return;
            }

            // 4. L2 Data Handling
            if (channel == "l2_data" || channel == "level2") {
                handle_l2_data(doc);
            }
        }

    private:
        void handle_l2_data(simdjson::dom::element& doc) {
            simdjson::dom::array events;
            if (doc["events"].get(events) != simdjson::SUCCESS) return;

            // Coinbase wraps updates in an 'events' array.
            for (auto event : events) {
                std::string_view type;
                if (event["type"].get(type) != simdjson::SUCCESS) continue;

                // 5. Snapshot vs Update Logic
                bool is_snapshot = (type == "snapshot");

                // If we aren't synchronized and this isn't a snapshot, 
                // we technically have a gap or started late. 
                if (!synchronized_ && !is_snapshot) {
                    return; 
                }

                if (is_snapshot) {
                    std::cout << "[Coinbase] Snapshot received. Synchronized." << std::endl;
                    synchronized_ = true;
                }

                // 6. Process Updates Array
                simdjson::dom::array updates;
                if (event["updates"].get(updates) != simdjson::SUCCESS) continue;

                for (auto update_value : updates) {
                    simdjson::dom::object update;
                    if (update_value.get(update) == simdjson::SUCCESS) {
                        push_update(update, is_snapshot);
                    }
                }
            }
        }

        void push_update(simdjson::dom::object update, bool is_snapshot) {
            
            std::string_view side_str, price_str, qty_str;
            
            // Extract fields: "side", "price_level", "new_quantity"
            if (update["side"].get(side_str) != simdjson::SUCCESS) return;
            if (update["price_level"].get(price_str) != simdjson::SUCCESS) return;
            if (update["new_quantity"].get(qty_str) != simdjson::SUCCESS) return;

            // 7. Side Parsing Optimization
            bool is_bid = (!side_str.empty() && side_str[0] == 'b');

            // 8. Numeric Conversion
            double p = std::stod(std::string(price_str));
            double q = std::stod(std::string(qty_str));

            // 9. Tick Construction
            BinaryTick t;
            t.timestamp = utils::rdtsc(); // Capture hardware timestamp
            t.price = static_cast<int64_t>(p * constants::PRICE_SCALE_DBL);
            t.quantity = static_cast<int64_t>(q * constants::PRICE_SCALE_DBL);
            t.is_bid = is_bid;
            t.symbol = 0; // Hardcoded ID for BTC-USD
            t.is_trade = false; // L2 update, not a trade
            t.is_snapshot = is_snapshot; // Flag to tell engine to Reset book if true

            // 10. Buffer Push (Spin-wait)
            while (!output_buffer_.push(t)) {
                utils::cpu_relax(); // Intel intrinsic for spin-loop hint
            }
        }

        // Helper to send subscription JSON
        void subscribe() {
            // Subscription payload for Level 2 data.
            std::string sub_msg = R"({
                "type": "subscribe",
                "product_ids": ["BTC-USD"],
                "channel": "level2"
            })";
            webSocket_.send(sub_msg);

            // Separate subscription for heartbeats
            std::string hb_msg = R"({
                "type": "subscribe",
                "product_ids": ["BTC-USD"],
                "channel": "heartbeats"
            })";
            webSocket_.send(hb_msg);
        }
    };
}
