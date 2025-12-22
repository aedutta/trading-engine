#include "execution/CoinbaseAuth.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/core_names.h>
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <immintrin.h>
#include <cstring>
#include <fstream>
#include "simdjson.h"

namespace hft {

    CoinbaseAuth::CoinbaseAuth(const std::string& key_name, const std::string& private_key) {
        // Try loading from file if not provided
        std::string loaded_key_name = key_name;
        std::string loaded_private_key = private_key;

        if (loaded_key_name.empty() || loaded_private_key.empty()) {
            // Try environment variables first
            if (loaded_key_name.empty()) loaded_key_name = get_key_name_from_env();
            if (loaded_private_key.empty()) loaded_private_key = get_private_key_from_env();

            // If still empty, try loading from file
            if (loaded_key_name.empty() || loaded_private_key.empty()) {
                std::ifstream f("private/cdp_api_key.json");
                if (f.good()) {
                    std::cout << "[Auth] Loading keys from private/cdp_api_key.json..." << std::endl;
                    simdjson::dom::parser parser;
                    simdjson::dom::element doc;
                    try {
                        auto json = parser.load("private/cdp_api_key.json");
                        loaded_key_name = std::string(json["name"].get_c_str());
                        loaded_private_key = std::string(json["privateKey"].get_c_str());
                    } catch (const std::exception& e) {
                        std::cerr << "[Auth] Failed to parse key file: " << e.what() << std::endl;
                    }
                }
            }
        }

        key_name_ = loaded_key_name;
        if (key_name_.empty()) {
            std::cerr << "[Auth] Error: COINBASE_KEY_NAME not set and could not be loaded from file." << std::endl;
        }

        std::string key_pem = loaded_private_key;
        if (key_pem.empty()) {
            std::cerr << "[Auth] Error: COINBASE_PRIVATE_KEY not set and could not be loaded from file." << std::endl;
            return;
        }

        BIO* bio = BIO_new_mem_buf(key_pem.c_str(), key_pem.length());
        pkey_ = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);

        if (!pkey_) {
            std::cerr << "Failed to parse Coinbase Private Key." << std::endl;
            ERR_print_errors_fp(stderr);
            return;
        }

        // Extract EC Key components using OpenSSL 3.0 compatible API
        // 1. Create Group (NIST P-256)
        group_ = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
        if (!group_) {
            std::cerr << "Failed to create EC_GROUP for P-256." << std::endl;
            return;
        }
        order_n_ = EC_GROUP_get0_order(group_);

        // 2. Extract Private Key 'd'
        if (!EVP_PKEY_get_bn_param(pkey_, OSSL_PKEY_PARAM_PRIV_KEY, &priv_key_d_)) {
             std::cerr << "Failed to extract private key scalar." << std::endl;
             return;
        }

        // Initialize hot path BIGNUMs
        ctx_ = BN_CTX_new();
        bn_z_ = BN_new();
        bn_s_ = BN_new();
        bn_tmp_ = BN_new();

        // Start pre-computation
        running_ = true;
        precompute_thread_ = std::thread(&CoinbaseAuth::precompute_worker, this);
    }

    CoinbaseAuth::~CoinbaseAuth() {
        running_ = false;
        if (precompute_thread_.joinable()) {
            precompute_thread_.join();
        }

        if (ctx_) BN_CTX_free(ctx_);
        if (bn_z_) BN_free(bn_z_);
        if (bn_s_) BN_free(bn_s_);
        if (bn_tmp_) BN_free(bn_tmp_);
        
        if (group_) EC_GROUP_free(group_);
        if (priv_key_d_) BN_free(priv_key_d_);
        if (pkey_) EVP_PKEY_free(pkey_);

        // Cleanup queue
        PrecomputedData data;
        while (precompute_queue_.pop(data)) {
            BN_free(data.k_inv);
            BN_free(data.r);
        }
    }

    std::string CoinbaseAuth::get_key_name_from_env() {
        const char* env_p = std::getenv("COINBASE_KEY_NAME");
        if (env_p) {
            return std::string(env_p);
        }
        return "";
    }

    std::string CoinbaseAuth::get_private_key_from_env() {
        const char* env_p = std::getenv("COINBASE_PRIVATE_KEY");
        if (env_p) {
            return std::string(env_p);
        }
        return "";
    }

    std::string CoinbaseAuth::generate_nonce() {
        unsigned char buf[16];
        if (RAND_bytes(buf, sizeof(buf)) != 1) {
            return "";
        }
        
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 16; ++i) {
            ss << std::setw(2) << (unsigned int)buf[i];
        }
        return ss.str();
    }

    std::string CoinbaseAuth::base64_url_encode(const std::string& data) {
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);

        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // No newlines
        BIO_write(bio, data.c_str(), data.length());
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        
        std::string encoded(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);

        // Convert to Base64Url
        // + -> -
        // / -> _
        // Remove =
        std::replace(encoded.begin(), encoded.end(), '+', '-');
        std::replace(encoded.begin(), encoded.end(), '/', '_');
        encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end());

        return encoded;
    }

    void CoinbaseAuth::precompute_worker() {
        BN_CTX* ctx = BN_CTX_new();
        BIGNUM* k = BN_new();
        EC_POINT* R = EC_POINT_new(group_);
        
        while (running_) {
            BIGNUM* k_inv = BN_new();
            BIGNUM* r = BN_new();
            
            // 1. Generate random k
            do {
                if (!BN_rand_range(k, order_n_)) {
                    // Error handling?
                }
            } while (BN_is_zero(k));

            // 2. R = k * G
            if (!EC_POINT_mul(group_, R, k, NULL, NULL, ctx)) {
                 // Error
            }

            // 3. r = R.x
            if (!EC_POINT_get_affine_coordinates(group_, R, r, NULL, ctx)) {
                // Error
            }
            
            // 4. r = r mod n
            if (!BN_nnmod(r, r, order_n_, ctx)) {
                 // Error
            }

            // 5. k_inv = k^-1 mod n
            if (!BN_mod_inverse(k_inv, k, order_n_, ctx)) {
                 // Error
            }

            PrecomputedData data = {k_inv, r};
            
            // Spin until we can push or stop
            while (running_ && !precompute_queue_.push(data)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            if (!running_) {
                BN_free(k_inv);
                BN_free(r);
                break;
            }
        }
        
        EC_POINT_free(R);
        BN_free(k);
        BN_CTX_free(ctx);
    }

    std::string CoinbaseAuth::sign_message_optimized(const std::string& message) {
        PrecomputedData data;
        
        // Spin wait for precomputed data
        int retries = 0;
        while (!precompute_queue_.pop(data)) {
            if (retries++ > 10000) {
                 std::cerr << "Precompute queue empty! Latency spike imminent." << std::endl;
                 std::this_thread::yield();
            }
            _mm_pause();
        }

        // 1. Hash message -> z
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((const unsigned char*)message.c_str(), message.length(), hash);
        BN_bin2bn(hash, SHA256_DIGEST_LENGTH, bn_z_);

        // 2. s = k_inv * (z + r * d) mod n
        
        // tmp = r * d mod n
        BN_mod_mul(bn_tmp_, data.r, priv_key_d_, order_n_, ctx_);
        
        // tmp = z + tmp mod n
        BN_mod_add(bn_tmp_, bn_z_, bn_tmp_, order_n_, ctx_);
        
        // s = k_inv * tmp mod n
        BN_mod_mul(bn_s_, data.k_inv, bn_tmp_, order_n_, ctx_);

        // 3. Encode r and s
        std::vector<unsigned char> sig_bytes(64);
        BN_bn2binpad(data.r, sig_bytes.data(), 32);
        BN_bn2binpad(bn_s_, sig_bytes.data() + 32, 32);

        std::string sig_str(reinterpret_cast<char*>(sig_bytes.data()), 64);
        
        // Cleanup precomputed data
        BN_free(data.k_inv);
        BN_free(data.r);

        return base64_url_encode(sig_str);
    }


    void CoinbaseAuth::base64_url_encode_to_buffer(const unsigned char* data, size_t len, char* out, size_t& out_len) {
        static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        
        size_t i = 0;
        size_t j = 0;
        
        while (i < len) {
            uint32_t octet_a = i < len ? data[i++] : 0;
            uint32_t octet_b = i < len ? data[i++] : 0;
            uint32_t octet_c = i < len ? data[i++] : 0;

            uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

            out[j++] = lookup[(triple >> 3 * 6) & 0x3F];
            out[j++] = lookup[(triple >> 2 * 6) & 0x3F];
            out[j++] = lookup[(triple >> 1 * 6) & 0x3F];
            out[j++] = lookup[(triple >> 0 * 6) & 0x3F];
        }

        // Padding adjustment for Base64Url (no padding, correct length)
        size_t mod = len % 3;
        if (mod == 1) j -= 2;
        else if (mod == 2) j -= 1;
        
        out[j] = '\0';
        out_len = j;
    }

    void CoinbaseAuth::generate_nonce_to_buffer(char* buf) {
        unsigned char rand_bytes[16];
        if (RAND_bytes(rand_bytes, sizeof(rand_bytes)) != 1) {
            // Fallback or error
        }
        
        static const char hex_chars[] = "0123456789abcdef";
        for (int i = 0; i < 16; ++i) {
            buf[i * 2] = hex_chars[(rand_bytes[i] >> 4) & 0xF];
            buf[i * 2 + 1] = hex_chars[rand_bytes[i] & 0xF];
        }
        buf[32] = '\0';
    }

    void CoinbaseAuth::sign_hash_optimized(const unsigned char* hash, char* out_sig_b64, size_t& out_len) {
        PrecomputedData data;
        
        // Spin wait for precomputed data
        int retries = 0;
        while (!precompute_queue_.pop(data)) {
            if (retries++ > 10000) {
                 std::this_thread::yield();
            }
            _mm_pause();
        }

        BN_bin2bn(hash, SHA256_DIGEST_LENGTH, bn_z_);

        // s = k_inv * (z + r * d) mod n
        BN_mod_mul(bn_tmp_, data.r, priv_key_d_, order_n_, ctx_);
        BN_mod_add(bn_tmp_, bn_z_, bn_tmp_, order_n_, ctx_);
        BN_mod_mul(bn_s_, data.k_inv, bn_tmp_, order_n_, ctx_);

        // Encode r and s
        unsigned char sig_bytes[64];
        BN_bn2binpad(data.r, sig_bytes, 32);
        BN_bn2binpad(bn_s_, sig_bytes + 32, 32);

        // Cleanup precomputed data
        BN_free(data.k_inv);
        BN_free(data.r);

        base64_url_encode_to_buffer(sig_bytes, 64, out_sig_b64, out_len);
    }

    void CoinbaseAuth::generate_jwt_zero_copy(const char* request_method, const char* request_path, const char* host, char* out_buffer, size_t max_len, size_t& out_len) {
        (void)max_len; // Suppress unused parameter warning
        // 1. Header
        // {"alg":"ES256","typ":"JWT","kid":"<key_name>","nonce":"<nonce>"}
        char nonce[33];
        generate_nonce_to_buffer(nonce);
        
        char header_json[256];
        int header_len = snprintf(header_json, sizeof(header_json), 
            "{\"alg\":\"ES256\",\"typ\":\"JWT\",\"kid\":\"%s\",\"nonce\":\"%s\"}", 
            key_name_.c_str(), nonce);
        
        size_t current_offset = 0;
        size_t segment_len = 0;

        // Write Header B64
        base64_url_encode_to_buffer((unsigned char*)header_json, header_len, out_buffer + current_offset, segment_len);
        current_offset += segment_len;
        out_buffer[current_offset++] = '.';

        // 2. Payload
        auto now = std::chrono::system_clock::now();
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        auto exp_sec = now_sec + 120;
        auto nbf_sec = now_sec - 10;

        char payload_json[512];
        int payload_len = snprintf(payload_json, sizeof(payload_json),
            "{\"iss\":\"cdp\",\"nbf\":%ld,\"exp\":%ld,\"sub\":\"%s\",\"uri\":\"%s %s%s\"}",
            nbf_sec, exp_sec, key_name_.c_str(), request_method, host, request_path);

        // Write Payload B64
        base64_url_encode_to_buffer((unsigned char*)payload_json, payload_len, out_buffer + current_offset, segment_len);
        current_offset += segment_len;
        
        // 3. Sign (Header.Payload)
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)out_buffer, current_offset, hash);
        
        out_buffer[current_offset++] = '.';
        
        // Write Signature B64
        sign_hash_optimized(hash, out_buffer + current_offset, segment_len);
        current_offset += segment_len;
        
        out_buffer[current_offset] = '\0';
        out_len = current_offset;
    }

}
