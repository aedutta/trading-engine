#pragma once

#include <string>
#include <string_view>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <thread>
#include <atomic>
#include "common/RingBuffer.hpp"

namespace hft {

    struct PrecomputedData {
        BIGNUM* k_inv;
        BIGNUM* r;
    };

    class CoinbaseAuth {
    public:
        CoinbaseAuth(const std::string& key_name = "", const std::string& private_key = "");
        ~CoinbaseAuth();

        /**
         * @brief Generates a JWT for Coinbase Advanced Trade (CDP) using zero-copy buffers.
         * 
         * @param request_method HTTP method (e.g., "GET", "POST").
         * @param request_path Request path (e.g., "/api/v3/brokerage/orders").
         * @param host Hostname (e.g., "api.cdp.coinbase.com").
         * @param out_buffer Buffer to write the JWT into.
         * @param max_len Maximum size of the buffer.
         * @param out_len Output parameter for the actual length of the JWT.
         */
        void generate_jwt_zero_copy(const char* request_method, const char* request_path, const char* host, char* out_buffer, size_t max_len, size_t& out_len);

    private:
        std::string base64_url_encode(const std::string& data);
        // Helper for zero-copy base64
        void base64_url_encode_to_buffer(const unsigned char* data, size_t len, char* out, size_t& out_len);
        
        std::string sign_message_optimized(const std::string& message);
        // Helper for zero-copy signing
        void sign_hash_optimized(const unsigned char* hash, char* out_sig_b64, size_t& out_len);

        std::string generate_nonce();
        void generate_nonce_to_buffer(char* buf);

        std::string get_private_key_from_env();
        std::string get_key_name_from_env();
        
        void precompute_worker();

        EVP_PKEY* pkey_ = nullptr;
        // EC_KEY* ec_key_ = nullptr; // Deprecated
        BIGNUM* priv_key_d_ = nullptr; // Owned
        EC_GROUP* group_ = nullptr; // Owned
        const BIGNUM* order_n_ = nullptr; // Owned by group
        
        std::string key_name_;

        // Pre-computation
        static constexpr size_t PRECOMPUTE_BUFFER_SIZE = 4096;
        RingBuffer<PrecomputedData, PRECOMPUTE_BUFFER_SIZE> precompute_queue_;
        std::thread precompute_thread_;
        std::atomic<bool> running_{false};
        
        // Hot path context and reusable BIGNUMs
        BN_CTX* ctx_ = nullptr;
        BIGNUM* bn_z_ = nullptr;
        BIGNUM* bn_s_ = nullptr;
        BIGNUM* bn_tmp_ = nullptr;
    };

}
