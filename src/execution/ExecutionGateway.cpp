#include "execution/ExecutionGateway.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <chrono>
#include <fstream>
#include <immintrin.h> // For _mm_pause

namespace hft {

    ExecutionGateway::ExecutionGateway(RingBuffer<Order, constants::RING_BUFFER_SIZE>& input_buffer)
        : input_buffer_(input_buffer) {
            latencies_.reserve(1000000); 
            executed_orders_.reserve(1000000);

            // Initialize Risk Manager with Paper Trading Balances
            // $100,000 USD and 10 BTC
            risk_manager_.set_balances(100000LL * 100000000LL, 10LL * 100000000LL);

            // Load root certificates (optional, for now we might skip or use default)
            // ssl_ctx_.set_default_verify_paths();
            ssl_ctx_.set_verify_mode(ssl::verify_none); // For sandbox/testing speed
        }

    ExecutionGateway::~ExecutionGateway() {
        if(stream_) {
            beast::error_code ec;
            stream_->shutdown(ec);
        }
    }

    void ExecutionGateway::start() {
        running_ = true;
        thread_ = std::thread(&ExecutionGateway::run, this);
        reconcile_thread_ = std::thread(&ExecutionGateway::reconcile_loop, this);
    }

    void ExecutionGateway::stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (reconcile_thread_.joinable()) {
            reconcile_thread_.join();
        }

        std::ofstream file("execution_latencies.csv");
        file << "latency_ns\n"; 
        for (const auto& lat : latencies_) {
            file << (lat / utils::CYCLES_PER_NS) << "\n";
        }
        file.close();

        std::ofstream trades_file("trades.csv");
        trades_file << "id,timestamp,price,quantity,is_buy\n";
        for (const auto& order : executed_orders_) {
            trades_file << order.id << "," 
                        << order.origin_timestamp << "," 
                        << order.price << "," 
                        << order.quantity << "," 
                        << (order.is_buy ? 1 : 0) << "\n";
        }
        trades_file.close();
    }

    void ExecutionGateway::update_jwt() {
        // Generate a new JWT valid for 2 minutes
        // We refresh it every 1 minute to be safe
        size_t jwt_len = 0;
        const std::string request_path = "/api/v3/brokerage/orders";
        const std::string host = "api.coinbase.com";
        
        auth_.generate_jwt_zero_copy("POST", request_path.c_str(), host.c_str(), jwt_buffer_, sizeof(jwt_buffer_), jwt_len);
        
        cached_jwt_ = std::string(jwt_buffer_, jwt_len);
        jwt_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    }

    void ExecutionGateway::connect() {
        try {
            tcp::resolver resolver(ioc_);
            auto const results = resolver.resolve("api.coinbase.com", "443");

            stream_ = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(ioc_, ssl_ctx_);

            // Set SNI Hostname (many hosts need this to handshake successfully)
            if(!SSL_set_tlsext_host_name(stream_->native_handle(), "api.coinbase.com")) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }

            beast::get_lowest_layer(*stream_).connect(results);
            
            // TCP_NODELAY
            beast::get_lowest_layer(*stream_).socket().set_option(tcp::no_delay(true));

            stream_->handshake(ssl::stream_base::client);
            std::cout << "[Exec] Connected to Coinbase Production via Boost.Beast" << std::endl;
        } catch (std::exception const& e) {
            std::cerr << "[Exec] Connection failed: " << e.what() << std::endl;
            stream_.reset();
        }
    }

    void ExecutionGateway::run() {
        utils::pin_thread_to_core(constants::EXECUTION_GATEWAY_CORE);

        connect();
        update_jwt(); // Initial JWT generation

        Order order;
        const std::string request_path = "/api/v3/brokerage/orders";
        const std::string host = "api.coinbase.com";
        
        // Reusable request object to minimize allocations
        http::request<http::buffer_body> req;
        req.method(http::verb::post);
        req.target(request_path);
        req.set(http::field::host, host);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::user_agent, "HFT-Engine/1.0");
        req.set(http::field::connection, "keep-alive"); // Explicit Keep-Alive

        beast::flat_buffer buffer; // Buffer for reading response

        while (running_) {
            if (input_buffer_.pop(order)) {
                std::cout << "[Exec] Order popped: " << order.id << std::endl;
                uint64_t pop_time = utils::rdtsc();

                if (!risk_manager_.check_and_reserve(order)) {
                    std::cerr << "[Exec] Risk check failed for order " << order.id << std::endl;
                    continue;
                }

                // Rate Limit Check (Token Bucket)
                if (!rate_limiter_.consume(1.0)) {
                    std::cerr << "[Exec] Rate limit hit, dropping order " << order.id << std::endl;
                    risk_manager_.rollback_order(order);
                    continue;
                }

                // Reconnect if needed
                if (!stream_) {
                    connect();
                    if (!stream_) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                }

                // Check JWT Expiry
                if (std::chrono::steady_clock::now() > jwt_expiry_) {
                    update_jwt();
                }

                // 3. Construct JSON Body (Zero-Copy)
                int payload_len = snprintf(payload_buffer_, sizeof(payload_buffer_),
                    "{\"client_order_id\":\"%lu\",\"product_id\":\"BTC-USDT\",\"side\":\"%s\",\"order_configuration\":{\"limit_limit_gtc\":{\"base_size\":\"%.8f\",\"limit_price\":\"%.2f\"}}}",
                    order.id,
                    order.is_buy ? "BUY" : "SELL",
                    (double)order.quantity / 100000000.0,
                    (double)order.price / 100000000.0
                );

                // 4. Setup Request
                snprintf(auth_header_buffer_, sizeof(auth_header_buffer_), "Bearer %s", cached_jwt_.c_str());
                req.set(http::field::authorization, auth_header_buffer_);
                
                req.body().data = payload_buffer_;
                req.body().size = payload_len;
                req.prepare_payload(); // Sets Content-Length

                // 5. Send
                try {
                    http::write(*stream_, req);
                    
                    // 6. Read Response
                    http::response<http::string_body> res;
                    http::read(*stream_, buffer, res);
                    
                    uint64_t end_time = utils::rdtsc();
                    
                    std::cout << "[Exec] Sent order " << order.id << ". Status: " << res.result_int() << std::endl;

                    // Check for Connection: close
                    if (res.keep_alive() == false) {
                        std::cout << "[Exec] Server requested close. Reconnecting..." << std::endl;
                        stream_.reset();
                    }

                    // Parse Response with simdjson
                    try {
                        simdjson::dom::element doc = json_parser_.parse(res.body());
                        (void)doc; // Suppress unused variable warning
                        if (res.result_int() == 200) {
                            // Extract order_id if needed
                            // std::string_view order_id = doc["order_id"];
                        } else {
                            std::cerr << "[Exec] Error response: " << res.body() << std::endl;
                        }
                    } catch (simdjson::simdjson_error& e) {
                        std::cerr << "[Exec] JSON parse error: " << e.what() << std::endl;
                    }
                    
                    uint64_t latency = end_time - pop_time;
                    if (latencies_.size() < latencies_.capacity()) latencies_.push_back(latency);
                    
                    if (res.result_int() == 200) {
                        executed_orders_.push_back(order);
                    } else {
                        risk_manager_.rollback_order(order);
                    }

                } catch (std::exception const& e) {
                    std::cerr << "[Exec] Request failed: " << e.what() << std::endl;
                    stream_.reset();
                    risk_manager_.rollback_order(order);
                }
            } else {
                _mm_pause();
            }
        }
    }

    void ExecutionGateway::reconcile_loop() {
        // Separate IO context and SSL context for reconciliation
        net::io_context ioc;
        ssl::context ssl_ctx{ssl::context::tlsv13_client};
        ssl_ctx.set_verify_mode(ssl::verify_none);
        
        CoinbaseAuth auth; // Load keys from env
        simdjson::dom::parser parser;

        while (running_) {
            try {
                // Sleep for 5 seconds
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (!running_) break;

                tcp::resolver resolver(ioc);
                auto const results = resolver.resolve("api.coinbase.com", "443");
                beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

                if(!SSL_set_tlsext_host_name(stream.native_handle(), "api.coinbase.com")) {
                    throw beast::system_error{beast::error_code{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()}};
                }

                beast::get_lowest_layer(stream).connect(results);
                stream.handshake(ssl::stream_base::client);

                // GET /api/v3/brokerage/accounts
                std::string request_path = "/api/v3/brokerage/accounts";
                std::string host = "api.coinbase.com";
                
                char jwt_buf[1024];
                size_t jwt_len = 0;
                auth.generate_jwt_zero_copy("GET", request_path.c_str(), host.c_str(), jwt_buf, sizeof(jwt_buf), jwt_len);

                http::request<http::string_body> req{http::verb::get, request_path, 11};
                req.set(http::field::host, host);
                req.set(http::field::user_agent, "HFT-Engine/1.0");
                req.set(http::field::authorization, std::string("Bearer ") + std::string(jwt_buf, jwt_len));

                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                http::read(stream, buffer, res);

                if (res.result() == http::status::ok) {
                    simdjson::dom::element doc = parser.parse(res.body());
                    simdjson::dom::array accounts = doc["accounts"];
                    
                    int64_t usd_bal = 0;
                    int64_t btc_bal = 0;

                    for (simdjson::dom::element account : accounts) {
                        std::string_view currency = account["currency"];
                        if (currency == "USD" || currency == "USDC") {
                            // Parse available_balance.value
                            double val = std::stod(std::string(account["available_balance"]["value"]));
                            usd_bal += static_cast<int64_t>(val * 100000000); // Convert to 1e8 fixed point
                        } else if (currency == "BTC") {
                            double val = std::stod(std::string(account["available_balance"]["value"]));
                            btc_bal += static_cast<int64_t>(val * 100000000);
                        }
                    }
                    
                    risk_manager_.set_balances(usd_bal, btc_bal);
                    // std::cout << "[Reconcile] Updated balances: USD=" << usd_bal << " BTC=" << btc_bal << std::endl;
                }

                beast::error_code ec;
                stream.shutdown(ec);
            } catch (std::exception const& e) {
                // std::cerr << "[Reconcile] Error: " << e.what() << std::endl;
            }
        }
    }

}
