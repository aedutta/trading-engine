#include "strategy/RiskManager.hpp"
#include "common/Types.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Running RiskManager Unit Test..." << std::endl;

    hft::RiskManager risk_manager;

    // Test Case: $1,000,000 Order
    // Price: $50,000
    // Quantity: 20 BTC
    // Value: $1,000,000
    
    hft::Order huge_order;
    huge_order.id = 1;
    huge_order.origin_timestamp = 0;
    huge_order.is_buy = true;
    
    // 1 BTC = 100,000,000 Satoshis (1e8)
    // Price = $50,000 * 1e8
    huge_order.price = 50000LL * 100000000LL; 
    
    // Quantity = 20 BTC * 1e8
    huge_order.quantity = 20LL * 100000000LL; 

    // Also set a reference price so we don't fail on "Fat Finger" if ref price is 0 (though implementation checks if ref_price > 0)
    // If ref_price is 0, fat finger check is skipped.
    // Let's set a reference price close to the order price to isolate the Notional/Clip check.
    risk_manager.set_reference_price(huge_order.price);

    std::cout << "Checking Order: " << std::endl;
    std::cout << "  Price: " << huge_order.price << " (Satoshis)" << std::endl;
    std::cout << "  Quantity: " << huge_order.quantity << " (Satoshis)" << std::endl;

    bool result = risk_manager.check_order(huge_order);

    if (result == false) {
        std::cout << "[PASS] Order was rejected as expected." << std::endl;
    } else {
        std::cout << "[FAIL] Order was APPROVED! Risk check failed." << std::endl;
        return 1;
    }

    // Additional Test: Valid Order
    // Price: $50,000
    // Quantity: 0.001 BTC ($50 value)
    hft::Order valid_order = huge_order;
    valid_order.quantity = 100000LL; // 0.001 BTC * 1e8 = 100,000 Satoshis
    
    // Value = 50,000 * 0.001 = $50. Max Notional is $500. Max Clip is 0.01 BTC (1,000,000 Satoshis).
    // This should pass.
    
    if (risk_manager.check_order(valid_order)) {
        std::cout << "[PASS] Valid order was approved." << std::endl;
    } else {
        std::cout << "[FAIL] Valid order was REJECTED!" << std::endl;
        return 1;
    }

    return 0;
}
