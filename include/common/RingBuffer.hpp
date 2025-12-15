#pragma once

#include <atomic>
#include <cstddef>
#include <vector>
#include <new>

namespace hft {

    // Function: RingBuffer
    // Description: Lock-free Single-Producer-Single-Consumer Ring Buffer.
    //              Enforces power-of-2 size for bitwise operations.
    template<typename T, size_t Size>
    class RingBuffer {
        static_assert((Size & (Size - 1)) == 0, "Buffer size must be a power of 2");

        // Align each slot to 64 bytes to prevent false sharing between adjacent slots
        struct alignas(64) Slot {
            T value;
        };
        
        Slot buffer[Size];

        alignas(64) std::atomic<size_t> head{0}; 
        alignas(64) std::atomic<size_t> tail{0}; 

    public:
        // Function: push
        // Description: Pushes an item into the buffer.
        // Inputs: item - The data to push.
        // Outputs: Returns true if successful, false if buffer is full.
        bool push(const T& item) {
            size_t current_head = head.load(std::memory_order_relaxed);
            size_t next_head = (current_head + 1) & (Size - 1);

            if (next_head == tail.load(std::memory_order_acquire)) {
                return false; 
            }

            buffer[current_head].value = item;
            head.store(next_head, std::memory_order_release); 
            return true;
        }

        // Function: pop
        // Description: Pops an item from the buffer.
        // Inputs: item - Reference to store the popped data.
        // Outputs: Returns true if successful, false if buffer is empty.
        bool pop(T& item) {
            size_t current_tail = tail.load(std::memory_order_relaxed);
            
            if (current_tail == head.load(std::memory_order_acquire)) {
                return false; 
            }

            item = buffer[current_tail].value;
            tail.store((current_tail + 1) & (Size - 1), std::memory_order_release);
            return true;
        }

        size_t size() const {
            size_t current_head = head.load(std::memory_order_relaxed);
            size_t current_tail = tail.load(std::memory_order_relaxed);
            return (current_head - current_tail) & (Size - 1);
        }
    };

}
