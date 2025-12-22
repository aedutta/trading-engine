#pragma once

#include "common/Types.hpp"
#include <vector>
#include <memory>
#include <cassert>

namespace hft {

    template<typename T, size_t PoolSize>
    class ObjectPool {
    public:
        ObjectPool() {
            pool_.resize(PoolSize);
            for (size_t i = 0; i < PoolSize; ++i) {
                free_indices_.push_back(i);
            }
        }

        T* acquire() {
            if (free_indices_.empty()) {
                return nullptr; // Or expand
            }
            size_t idx = free_indices_.back();
            free_indices_.pop_back();
            return &pool_[idx];
        }

        void release(T* obj) {
            // Calculate index
            size_t idx = obj - &pool_[0];
            assert(idx < PoolSize);
            free_indices_.push_back(idx);
        }

    private:
        std::vector<T> pool_;
        std::vector<size_t> free_indices_;
    };

}