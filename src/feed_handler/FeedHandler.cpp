#include "feed_handler/FeedHandler.hpp"
#include "common/Utils.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <immintrin.h>

namespace hft {

    FeedHandler::FeedHandler(RingBuffer<BinaryTick, constants::RING_BUFFER_SIZE>& output_buffer)
        : output_buffer_(output_buffer), mapped_addr_(MAP_FAILED) {}

    FeedHandler::~FeedHandler() {
        stop();
        if (mapped_addr_ != MAP_FAILED) {
            munmap(mapped_addr_, file_size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }

    void FeedHandler::start() {
        running_ = true;
        thread_ = std::thread(&FeedHandler::run, this);
    }

    void FeedHandler::stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void FeedHandler::init(const std::string& filename) {
        fd_ = open(filename.c_str(), O_RDONLY);
        if (fd_ == -1) {
            std::cerr << "Failed to open binary data file: " << filename << std::endl;
            return;
        }

        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            std::cerr << "Failed to stat file" << std::endl;
            return;
        }
        file_size_ = sb.st_size;

        // Align file size to 2MB for Transparent Huge Pages (THP)
        // Note: We can't change the file size here, but we can ensure our mapping is aligned.
        // mmap typically returns page-aligned memory.
        // To fully leverage THP, the kernel needs to be configured, but we can try to hint it.
        
        // Use MAP_HUGETLB if available and configured (often requires root/admin setup), 
        // otherwise rely on THP with madvise(MADV_HUGEPAGE).
        
        mapped_addr_ = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd_, 0);
        if (mapped_addr_ == MAP_FAILED) {
            mapped_addr_ = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (mapped_addr_ == MAP_FAILED) {
                std::cerr << "mmap failed" << std::endl;
                return;
            }
        }

        // Hint for Transparent Huge Pages (if enabled in OS)
        madvise(mapped_addr_, file_size_, MADV_HUGEPAGE);
        // Hint for Sequential Access
        madvise(mapped_addr_, file_size_, MADV_SEQUENTIAL);

        size_t num_ticks = file_size_ / sizeof(BinaryTick);
        ticks_ = std::span<const BinaryTick>(
            reinterpret_cast<const BinaryTick*>(mapped_addr_), num_ticks
        );
        
        std::cout << "Feed Handler Initialized: Mapped " << num_ticks 
                  << " ticks (" << file_size_ / (1024.0 * 1024.0) << " MB) directly from disk." << std::endl;
    }

    void FeedHandler::run() {
        utils::pin_thread_to_core(constants::FEED_HANDLER_CORE);

        for (const auto& tick : ticks_) {
            if (!running_) break;

            BinaryTick t = tick;
            t.timestamp = utils::rdtsc();

            while (!output_buffer_.push(t) && running_) {
                _mm_pause();
            }
        }
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

}
