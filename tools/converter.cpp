#include "../include/common/Types.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <charconv>
#include <cstring>
#include <string_view>

using namespace hft;

void parse_line(std::string_view line, BinaryTick& tick) {
    // Format: id, price, qty, quote_qty, time, is_buyer_maker, is_best_match
    
    auto find_next = [&](size_t start) {
        return line.find(',', start);
    };

    size_t start = 0;
    size_t end = find_next(start);

    // 1. ID
    if (end != std::string_view::npos) {
        std::from_chars(line.data() + start, line.data() + end, tick.id);
        start = end + 1;
        end = find_next(start);
    }

    // 2. Price
    if (end != std::string_view::npos) {
        double temp_price;
        std::from_chars(line.data() + start, line.data() + end, temp_price);
        tick.price = static_cast<int64_t>(temp_price * 100000000); // Convert to Satoshis
        start = end + 1;
        end = find_next(start);
    }

    // 3. Quantity
    if (end != std::string_view::npos) {
        double temp_qty;
        std::from_chars(line.data() + start, line.data() + end, temp_qty);
        tick.quantity = static_cast<int64_t>(temp_qty * 100000000); // Convert to Satoshis
        start = end + 1;
        end = find_next(start);
    }

    // 4. Quote Quantity (Skip)
    if (end != std::string_view::npos) {
        start = end + 1;
        end = find_next(start);
    }

    // 5. Timestamp
    if (end != std::string_view::npos) {
        std::from_chars(line.data() + start, line.data() + end, tick.timestamp);
        start = end + 1;
        end = find_next(start);
    }

    // 6. is_buyer_maker (True = Sell/Bid, False = Buy/Ask)
    if (end != std::string_view::npos) {
        std::string_view is_bid_str = line.substr(start, end - start);
        tick.is_bid = (is_bid_str == "True");
    } else {
        // Handle case where it's the last field or something
        std::string_view is_bid_str = line.substr(start);
        tick.is_bid = (is_bid_str == "True");
    }

    // Symbol
    // Optimization: Store symbol as uint64_t
    const char* symbol_str = "BTCUSDT";
    tick.symbol = 0;
    std::memcpy(&tick.symbol, symbol_str, std::min(std::strlen(symbol_str), sizeof(uint64_t)));
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_csv> <output_bin>" << std::endl;
        return 1;
    }

    std::ifstream csv(argv[1]);
    std::ofstream bin(argv[2], std::ios::binary);

    if (!csv.is_open() || !bin.is_open()) {
        std::cerr << "Failed to open files." << std::endl;
        return 1;
    }

    std::string line;
    // Check for header
    if (std::getline(csv, line)) {
        if (!line.empty() && !isdigit(line[0])) {
            // It's a header, skip it
        } else {
            // Not a header, reset
            csv.clear();
            csv.seekg(0);
        }
    }

    uint64_t count = 0;
    while (std::getline(csv, line)) {
        BinaryTick tick{};
        parse_line(line, tick);
        bin.write(reinterpret_cast<char*>(&tick), sizeof(BinaryTick));
        count++;
        if (count % 1000000 == 0) {
            std::cout << "Processed " << count << " ticks..." << std::endl;
        }
    }

    std::cout << "Conversion complete. " << count << " ticks written." << std::endl;
    return 0;
}
