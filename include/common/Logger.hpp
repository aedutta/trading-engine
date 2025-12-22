#pragma once

#include "common/RingBuffer.hpp"
#include "common/Utils.hpp"
#include <atomic>
#include <thread>
#include <fstream>
#include <string>
#include <vector>

namespace hft {

    enum class LogLevel : uint8_t {
        INFO,
        WARNING,
        ERROR,
        DEBUG
    };

    struct LogEntry {
        uint64_t timestamp;
        LogLevel level;
        char message[128]; // Fixed size message
    };

    class AsyncLogger {
    public:
        static AsyncLogger& instance() {
            static AsyncLogger instance;
            return instance;
        }

        void start(const std::string& filename) {
            filename_ = filename;
            running_ = true;
            thread_ = std::thread(&AsyncLogger::run, this);
        }

        void stop() {
            running_ = false;
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        template<typename... Args>
        void log(LogLevel level, const char* fmt, Args... args) {
            LogEntry entry;
            entry.timestamp = utils::rdtsc();
            entry.level = level;
            snprintf(entry.message, sizeof(entry.message), fmt, args...);
            
            // Non-blocking push. If full, we drop the log (or spin, but dropping is safer for latency)
            if (!buffer_.push(entry)) {
                // Buffer full - drop or handle
            }
        }

        // Overload for no args to fix -Wformat-security
        void log(LogLevel level, const char* msg) {
            LogEntry entry;
            entry.timestamp = utils::rdtsc();
            entry.level = level;
            strncpy(entry.message, msg, sizeof(entry.message) - 1);
            entry.message[sizeof(entry.message) - 1] = '\0';
            
            if (!buffer_.push(entry)) {
            }
        }

    private:
        AsyncLogger() : running_(false) {}
        ~AsyncLogger() { stop(); }

        void run() {
            utils::pin_thread_to_core(constants::LOGGER_CORE);
            std::ofstream file(filename_, std::ios::out | std::ios::app);
            
            LogEntry entry;
            while (running_ || !buffer_.isEmpty()) {
                if (buffer_.pop(entry)) {
                    // Format: [Timestamp] [Level] Message
                    // Convert TSC to rough ns/us if needed, or just log raw TSC
                    file << entry.timestamp << " ";
                    switch (entry.level) {
                        case LogLevel::INFO: file << "[INFO] "; break;
                        case LogLevel::WARNING: file << "[WARN] "; break;
                        case LogLevel::ERROR: file << "[ERROR] "; break;
                        case LogLevel::DEBUG: file << "[DEBUG] "; break;
                    }
                    file << entry.message << "\n";
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            file.close();
        }

        RingBuffer<LogEntry, 8192> buffer_; // 8192 * 144 bytes ~= 1MB
        std::atomic<bool> running_;
        std::thread thread_;
        std::string filename_;
    };

}

// Macro for easy usage
#define LOG_INFO(fmt, ...) hft::AsyncLogger::instance().log(hft::LogLevel::INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) hft::AsyncLogger::instance().log(hft::LogLevel::WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) hft::AsyncLogger::instance().log(hft::LogLevel::ERROR, fmt, ##__VA_ARGS__)
