#include "log.hpp"

const std::filesystem::path Logger::logger_dir = "./log";
thread_local int current_buffer_idx_ = -1;
Logger &Logger::get_instance()
{
    static Logger instance;
    return instance;
}
Logger::Logger()
{
    backend_thread_ = std::jthread([this](std::stop_token stop_token)
                                   { backend_loop(stop_token); });
}
Logger::~Logger()
{
    if (current_buffer_idx_ != -1)
    {
        auto &buffer = buffer_pool_[current_buffer_idx_];
        if (!buffer.empty())
        {
            while (!free_buffers_.enqueue(&buffer))
                ;
            semaphore_.release();
        }
    }
    backend_thread_.request_stop();
    semaphore_.release();
    if (backend_thread_.joinable())
    {
        backend_thread_.join();
    }
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
}
void Logger::append(const char *data, size_t size)
{
    thread_local Logger::ThreadFlushGuard thread_flush_guard_;
    if (current_buffer_idx_ == -1 || buffer_pool_[current_buffer_idx_].size() + size > LoggerBuffer::BUFFER_SIZE)
    {
        size_t start = (current_buffer_idx_ == -1) ? 0 : (current_buffer_idx_ + 1) & mask_;
        if (current_buffer_idx_ != -1)
        {
            while (!free_buffers_.enqueue(&buffer_pool_[current_buffer_idx_]))
                ;
            semaphore_.release();
            current_buffer_idx_ = -1;
        }
        for (;;)
        {
            for (size_t j = 0; j < POOL_SIZE; ++j)
            {
                size_t i = (start + j) & mask_;
                bool expected = false;
                if (buffer_pool_[i].hold_.compare_exchange_weak(
                        expected, true, std::memory_order_acquire))
                {
                    current_buffer_idx_ = static_cast<int>(i);
                    goto found;
                }
            }
            std::this_thread::yield();
        }
    found:;
    }
    buffer_pool_[current_buffer_idx_].append(data, size);
}
void Logger::LoggerBuffer::append(const char *data, size_t size)
{
    memcpy(buffer_ + current_size_, data, size);
    current_size_ += size;
}
void Logger::log(LogLevel level, const std::string &message)
{
    if (level < get_instance().get_log_level())
        return;
    const char *level_str;
    switch (level)
    {
    case LogLevel::DEBUG:   level_str = "[DEBUG]";   break;
    case LogLevel::INFO:    level_str = "[INFO]";    break;
    case LogLevel::WARNING: level_str = "[WARNING]"; break;
    case LogLevel::ERROR:   level_str = "[ERROR]";   break;
    default:                level_str = "[UNKNOWN]"; break;
    }
    const auto ts = get_current_timestamp();
    std::string line = std::format("{} {} {}\n", ts.at("[YmdHMS]"), level_str, message);
    std::cout<< line;
    append(line.c_str(), line.size());
}
bool Logger::switch_log_file_date()
{
    if (!std::filesystem::exists(logger_dir))
    {
        std::filesystem::create_directories(logger_dir);
    }
    if (fd_ >= 0)
    {
        close(fd_);
    }
    fd_ = open((logger_dir / (today_ + ".log")).c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd_ >= 0)
    {
        struct stat st;
        if (fstat(fd_, &st) == 0)
        {
            offset_ = st.st_size;
        }
        return true;
    }
    return false;
}
void Logger::backend_loop(std::stop_token stop_token)
{
    auto submit_buffer = [this](LoggerBuffer *buffer)
    {
        auto sqe = ring_.get_sqe();
        sqe.prep_write(fd_, buffer->data(), buffer->size(), offset_);
        sqe.set_data(buffer);
        offset_ += buffer->size();
        ++in_flight_;
    };

    auto handle_cqe = [this](Cqe &cqe)
    {
        auto *buf = static_cast<LoggerBuffer *>(cqe.get_data());
        buf->clear();
        buf->hold_.store(false, std::memory_order_release);
        ring_.cqe_seen(cqe);
        if (in_flight_ > 0)
        {
            --in_flight_;
        }
    };
    today_ = get_current_timestamp().at("Ymd");
    switch_log_file_date();
    while (true)
    {
        if (!stop_token.stop_requested())
        {
            auto new_date = get_current_timestamp().at("Ymd");
            if (today_ != new_date)
            {
                today_ = new_date;
                if (in_flight_ > 0)
                {
                    ring_.submit_and_wait(in_flight_);
                    while (in_flight_ > 0)
                    {
                        auto cqe = ring_.peek_cqe();
                        handle_cqe(cqe);
                    }
                }
                switch_log_file_date();
            }
            semaphore_.acquire();
        }
        LoggerBuffer *buffer = nullptr;
        while (free_buffers_.dequeue(buffer))
        {
            submit_buffer(buffer);
        }
        ring_.submit();

        while (true)
        {
            auto cqe = ring_.peek_cqe();
            if (cqe.empty())
                break;
            handle_cqe(cqe);
        }

        if (stop_token.stop_requested())
        {
            while (free_buffers_.dequeue(buffer))
            {
                submit_buffer(buffer);
            }
            ring_.submit();
            while (in_flight_ > 0)
            {
                ring_.submit_and_wait(in_flight_);
                while (in_flight_ > 0)
                {
                    auto cqe = ring_.peek_cqe();
                    handle_cqe(cqe);
                }
            }
            break;
        }
    }
}
Logger::ThreadFlushGuard::~ThreadFlushGuard()
{
    auto &buffer = Logger::get_instance().buffer_pool_[current_buffer_idx_];
    if (!buffer.empty())
    {
        while (!Logger::get_instance().free_buffers_.enqueue(&buffer))
            ;
        Logger::get_instance().semaphore_.release();
    }
}