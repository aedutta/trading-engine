#include "execution/CoinbaseAuth.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include "simdjson.h"

// Helper to read file
std::string read_file(const std::string& path) {
    std::ifstream t(path);
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

// Curl write callback
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int main() {
    try {
        // 1. Load credentials
        std::string key_file_path = "../private/cdp_api_key.json";
        std::cout << "Loading key from: " << key_file_path << std::endl;
        std::string json_content = read_file(key_file_path);
        
        if (json_content.empty()) {
            std::cerr << "Failed to read key file or file is empty." << std::endl;
            return 1;
        }

        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(json_content);
        
        std::string key_name = std::string(doc["name"].get_string().value());
        std::string private_key = std::string(doc["privateKey"].get_string().value());

        // 2. Generate JWT
        hft::CoinbaseAuth auth(key_name, private_key);
        
        std::string request_method = "GET";
        std::string host = "api.cdp.coinbase.com";
        std::string request_path = "/platform/v2/evm/token-balances/base-sepolia/0x8fddcc0c5c993a1968b46787919cc34577d6dc5c";
        
        char jwt_buffer[1024];
        size_t jwt_len = 0;
        auth.generate_jwt_zero_copy(request_method.c_str(), request_path.c_str(), host.c_str(), jwt_buffer, sizeof(jwt_buffer), jwt_len);
        std::string jwt(jwt_buffer, jwt_len);
        
        std::cout << "Generated JWT: " << jwt << std::endl;

        // 3. Send Request
        CURL* curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if(curl) {
            std::string url = "https://" + host + request_path;
            struct curl_slist *headers = NULL;
            std::string auth_header = "Authorization: Bearer " + jwt;
            
            headers = curl_slist_append(headers, auth_header.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            
            // Verify SSL peer (optional, but good for security)
            // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

            std::cout << "Sending " << request_method << " request to " << url << "..." << std::endl;
            res = curl_easy_perform(curl);
            
            if(res != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            } else {
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                std::cout << "Status Code: " << response_code << std::endl;
                std::cout << "Response Body: " << readBuffer << std::endl;
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
