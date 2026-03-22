#pragma once
#include <string>
#include <string_view>
#include <atomic>
#include <format>
#include <filesystem>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <semaphore>
#include <sys/stat.h>
#include <unistd.h>
#include "util/timestamp.hpp"
#include "util/noncopyable.hpp"
#include "core/io_ring.hpp"
#include "util/mpsc_queue.hpp"

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

    void log(LogLevel level, std::string_view message);

    void set_log_level(LogLevel level)
    {
        log_level_.store(level, std::memory_order_relaxed);
    }
    LogLevel get_log_level() const
    {
        return log_level_.load(std::memory_order_relaxed);
    }

    static std::string &get_fmt_buf()
    {
        thread_local std::string buf;
        return buf;
    }

    ~Logger();

private:
    class LoggerBuffer
    {
    public:
        static constexpr size_t BUFFER_SIZE = 1024 * 1024 * 32;
        bool empty() const { return current_size_ == 0; }
        size_t size() const { return current_size_; }
        void append(const char *data, size_t size);
        char *data() { return buffer_; }
        void clear() { current_size_ = 0; }
        std::atomic<bool> in_flight_{false};

    private:
        char buffer_[BUFFER_SIZE];
        size_t current_size_ = 0;
    };
    struct ThreadState
    {
        LoggerBuffer bufs[2];
        int write_idx = 0;
        std::chrono::steady_clock::time_point last_flush_time = std::chrono::steady_clock::now();
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

    static ThreadState *&thread_state();

    static constexpr size_t FLUSH_QUEUE_CAPACITY = 128;
    MPSCQueue<LoggerBuffer *, FLUSH_QUEUE_CAPACITY> flush_queue_;
    std::counting_semaphore<FLUSH_QUEUE_CAPACITY> semaphore_{0};

    std::jthread backend_thread_;
    std::string today_;
    int fd_ = -1;
    size_t offset_ = 0;
    unsigned in_flight_ = 0;
    IoRing ring_;
    TimestampCache ts_cache_;
    std::atomic<LogLevel> log_level_{LogLevel::DEBUG};
};

inline void log_error(std::string_view message)
{
    Logger::get_instance().log(Logger::LogLevel::ERROR, message);
}
inline void log_warning(std::string_view message)
{
    Logger::get_instance().log(Logger::LogLevel::WARNING, message);
}
inline void log_info(std::string_view message)
{
    Logger::get_instance().log(Logger::LogLevel::INFO, message);
}
inline void log_debug(std::string_view message)
{
    Logger::get_instance().log(Logger::LogLevel::DEBUG, message);
}

#define LOG_ERROR(...) do { \
    if (Logger::get_instance().get_log_level() <= Logger::LogLevel::ERROR) { \
        auto& _b = Logger::get_fmt_buf(); _b.clear(); \
        std::format_to(std::back_inserter(_b), __VA_ARGS__); \
        Logger::get_instance().log(Logger::LogLevel::ERROR, _b); \
    } \
} while(0)
#define LOG_WARNING(...) do { \
    if (Logger::get_instance().get_log_level() <= Logger::LogLevel::WARNING) { \
        auto& _b = Logger::get_fmt_buf(); _b.clear(); \
        std::format_to(std::back_inserter(_b), __VA_ARGS__); \
        Logger::get_instance().log(Logger::LogLevel::WARNING, _b); \
    } \
} while(0)
#define LOG_INFO(...) do { \
    if (Logger::get_instance().get_log_level() <= Logger::LogLevel::INFO) { \
        auto& _b = Logger::get_fmt_buf(); _b.clear(); \
        std::format_to(std::back_inserter(_b), __VA_ARGS__); \
        Logger::get_instance().log(Logger::LogLevel::INFO, _b); \
    } \
} while(0)
#define LOG_DEBUG(...) do { \
    if (Logger::get_instance().get_log_level() <= Logger::LogLevel::DEBUG) { \
        auto& _b = Logger::get_fmt_buf(); _b.clear(); \
        std::format_to(std::back_inserter(_b), __VA_ARGS__); \
        Logger::get_instance().log(Logger::LogLevel::DEBUG, _b); \
    } \
} while(0)
