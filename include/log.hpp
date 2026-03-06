#pragma once
#include <mutex>
#include <string>
#include <atomic>
#include <format>
#include <filesystem>
#include <fcntl.h>
#include <thread>
#include <cstring>
#include <semaphore>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include "timestamp.hpp"
#include "noncopyable.hpp"
#include "io_ring.hpp"
#include "MPSC_queue.hpp"

class Logger : NonCopyable
{
public:
    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    static const std::filesystem::path logger_dir;

    static Logger &get_instance();

    void log(LogLevel level, const std::string &message);

    void set_log_level(LogLevel level)
    {
        log_level_.store(level, std::memory_order_relaxed);
    }
    LogLevel get_log_level() const
    {
        return log_level_.load(std::memory_order_relaxed);
    }

    ~Logger();

private:
    class LoggerBuffer
    {
    public:
        constexpr static size_t BUFFER_SIZE = 1024 * 1024 * 4;
        bool empty() const { return current_size_ == 0; }
        size_t size() const { return current_size_; }
        void append(const char *, size_t);
        char *data() { return buffer_; }
        void clear() { current_size_ = 0; }
        std::atomic<bool> hold_{false};

    private:
        char buffer_[BUFFER_SIZE];
        size_t current_size_ = 0;
    };
    class ThreadFlushGuard
    {
    public:
        ThreadFlushGuard() = default;
        ~ThreadFlushGuard();
    };
    Logger();
    void append(const char *data, size_t size);
    void backend_loop(std::stop_token stop_token);
    bool switch_log_file_date();
    static constexpr size_t POOL_SIZE = 16;
    size_t mask_ = POOL_SIZE - 1;

    LoggerBuffer buffer_pool_[POOL_SIZE];
    MPSCQueue<LoggerBuffer *, POOL_SIZE> free_buffers_;

    std::counting_semaphore<POOL_SIZE> semaphore_{0};
    std::jthread backend_thread_;

    std::string today_;
    int fd_ = -1;
    size_t offset_ = 0;
    unsigned in_flight_ = 0;
    IoRing ring_;
    std::atomic<LogLevel> log_level_{LogLevel::DEBUG};
};