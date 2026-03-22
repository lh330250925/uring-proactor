#include "util/log.hpp"

const std::filesystem::path Logger::logger_dir = "./log";
Logger::ThreadState *&Logger::thread_state()
{
    thread_local ThreadState *ts = nullptr;
    return ts;
}
Logger &Logger::get_instance()
{
    static Logger instance;
    return instance;
}
Logger::Logger() : ring_(64)
{
    backend_thread_ = std::jthread([this](std::stop_token st)
                                   { backend_loop(st); });
}
Logger::~Logger()
{
    ThreadState *main_ts = thread_state();
    thread_state() = nullptr;
    if (main_ts)
    {
        auto &front = main_ts->bufs[main_ts->write_idx];
        if (!front.empty())
        {
            int back_idx = 1 - main_ts->write_idx;
            while (main_ts->bufs[back_idx].in_flight_.load(std::memory_order_acquire))
                std::this_thread::yield();
            front.in_flight_.store(true, std::memory_order_release);
            while (!flush_queue_.enqueue(&front))
                std::this_thread::yield();
            semaphore_.release();
        }
    }
    backend_thread_.request_stop();
    semaphore_.release();
    if (backend_thread_.joinable())
        backend_thread_.join();
    delete main_ts;
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
}
void Logger::append(const char *data, size_t size)
{
    thread_local ThreadFlushGuard guard;

    auto &ts = thread_state();
    if (!ts)
        ts = new ThreadState();

    auto &front = ts->bufs[ts->write_idx];

    const auto now = std::chrono::steady_clock::now();
    const bool is_full = front.size() + size > LoggerBuffer::BUFFER_SIZE;
    const bool timed_out = !is_full && !front.empty() &&
        (now - ts->last_flush_time) >= std::chrono::seconds(1);

    if (is_full || timed_out)
    {
        const int back_idx = 1 - ts->write_idx;
        if (!ts->bufs[back_idx].in_flight_.load(std::memory_order_acquire))
        {
            front.in_flight_.store(true, std::memory_order_release);
            if (!flush_queue_.enqueue(&front))
            {
                front.in_flight_.store(false, std::memory_order_release);
                if (is_full) return;
            }
            else
            {
                semaphore_.release();
                ts->write_idx = back_idx;
                ts->last_flush_time = now;
            }
        }
        else if (is_full)
        {
            return;
        }
    }
    ts->bufs[ts->write_idx].append(data, size);
}
void Logger::LoggerBuffer::append(const char *data, size_t size)
{
    memcpy(buffer_ + current_size_, data, size);
    current_size_ += size;
}
void Logger::log(LogLevel level, std::string_view message)
{
    if (level < get_log_level())
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
    char ts_buf[24];
    ts_cache_.get_datetime(ts_buf);
    char line[320];
    int n = snprintf(line, sizeof(line), "%s %s %.*s\n",
                     ts_buf, level_str, (int)message.size(), message.data());
    if (n > 0)
        append(line, n < (int)sizeof(line) ? (size_t)n : sizeof(line) - 1);
}
bool Logger::switch_log_file_date()
{
    if (!std::filesystem::exists(logger_dir))
        std::filesystem::create_directories(logger_dir);
    if (fd_ >= 0)
        close(fd_);
    fd_ = open((logger_dir / (today_ + ".log")).c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd_ >= 0)
    {
        struct stat st;
        if (fstat(fd_, &st) == 0)
            offset_ = st.st_size;
        return true;
    }
    return false;
}

void Logger::backend_loop(std::stop_token stop_token)
{
    auto submit_buf = [this](LoggerBuffer *buf)
    {
        if (fd_ < 0)
        {
            buf->clear();
            buf->in_flight_.store(false, std::memory_order_release);
            return;
        }
        auto sqe = ring_.get_sqe();
        sqe.prep_write(fd_, buf->data(), buf->size(), offset_);
        sqe.set_data(buf);
        offset_ += buf->size();
        ++in_flight_;
    };

    auto handle_cqe = [this](Cqe &cqe)
    {
        auto *buf = static_cast<LoggerBuffer *>(cqe.get_data());
        buf->clear();
        buf->in_flight_.store(false, std::memory_order_release);
        ring_.cqe_seen(cqe);
        --in_flight_;
    };

    char date_buf[12];
    ts_cache_.get_date(date_buf);
    today_ = date_buf;
    switch_log_file_date();

    while (true)
    {
        if (!stop_token.stop_requested())
        {
            char new_date[12];
            ts_cache_.get_date(new_date);
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

        LoggerBuffer *buf = nullptr;
        while (flush_queue_.dequeue(buf))
            submit_buf(buf);
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
            while (flush_queue_.dequeue(buf))
                submit_buf(buf);
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
    auto &ts = thread_state();
    if (!ts)
        return;

    Logger &lg = Logger::get_instance();
    auto &front = ts->bufs[ts->write_idx];

    if (!front.empty())
    {
        const int back_idx = 1 - ts->write_idx;
        if (!ts->bufs[back_idx].in_flight_.load(std::memory_order_acquire))
        {
            front.in_flight_.store(true, std::memory_order_release);
            if (lg.flush_queue_.enqueue(&front))
                lg.semaphore_.release();
            else
                front.in_flight_.store(false, std::memory_order_release);
        }
    }
    for (auto &buf : ts->bufs)
        while (buf.in_flight_.load(std::memory_order_acquire))
            std::this_thread::yield();

    delete ts;
    ts = nullptr;
}