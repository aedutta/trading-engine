#pragma once

#include "common/RingBuffer.hpp"
#include "common/Types.hpp"
#include "common/Utils.hpp"
#include "execution/CoinbaseAuth.hpp"
#include "strategy/RiskManager.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <simdjson.h>

namespace beast = boost::beast;     // from <boost/beast.hpp>
namespace http = beast::http;       // from <boost/beast/http.hpp>
namespace net = boost::asio;        // from <boost/asio.hpp>
namespace ssl = net::ssl;           // from <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;           // from <boost/asio/ip/tcp.hpp>

namespace hft {

    struct TokenBucket {
        double tokens;
        double max_tokens;
        double refill_rate; // tokens per second
        std::chrono::steady_clock::time_point last_refill;

        TokenBucket(double max, double rate) 
            : tokens(max), max_tokens(max), refill_rate(rate), last_refill(std::chrono::steady_clock::now()) {}

        bool consume(double count = 1.0) {
            refill();
            if (tokens >= count) {
                tokens -= count;
                return true;
            }
            return false;
        }

        void refill() {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - last_refill;
            double new_tokens = elapsed.count() * refill_rate;
            if (new_tokens > 0) {
                tokens = std::min(max_tokens, tokens + new_tokens);
                last_refill = now;
            }
        }
    };

    // Function: ExecutionGateway
    // Description: Handles order formatting and transmission.
    class ExecutionGateway {
    public:
        // Function: ExecutionGateway
        // Description: Constructor.
        // Inputs: input_buffer - Source of orders.
        ExecutionGateway(RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer);

        ~ExecutionGateway();

        // Function: start
        // Description: Starts the execution thread.
        void start();

        // Function: stop
        // Description: Stops the execution thread.
        void stop();

    private:
        void run();
        void connect();
        void reconcile_loop();

        RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer_;
        std::atomic<bool> running_{false};
        std::thread thread_;
        std::thread reconcile_thread_;
        std::vector<uint64_t> latencies_; 
        std::vector<Order> executed_orders_;

        // Boost Beast & Auth
        net::io_context ioc_;
        ssl::context ssl_ctx_{ssl::context::tlsv13_client};
        std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> stream_;
        
        CoinbaseAuth auth_;
        RiskManager risk_manager_;
        TokenBucket rate_limiter_{10.0, 10.0}; // 10 burst, 10/sec
        simdjson::dom::parser json_parser_;

        // Pre-allocated buffers
        char jwt_buffer_[1024];
        char payload_buffer_[1024];
        char auth_header_buffer_[1200];
    };

}
