#include "util/timestamp.hpp"

void TimestampCache::maybe_update() const
{
    const time_t now = ::time(nullptr);
    time_t expected = cached_second_.load(std::memory_order_relaxed);
    if (now == expected)
        return;
    if (!cached_second_.compare_exchange_strong(
            expected, now, std::memory_order_relaxed))
        return;

    std::tm tm;
    ::localtime_r(&now, &tm);
    char dt[24]{}, d[12]{};
    ::strftime(dt, sizeof(dt), "[%Y-%m-%d %H:%M:%S]", &tm);
    ::strftime(d, sizeof(d), "%Y-%m-%d", &tm);

    seq_.fetch_add(1, std::memory_order_release);
    std::memcpy(datetime_str_, dt, sizeof(dt));
    std::memcpy(date_str_, d, sizeof(d));
    seq_.fetch_add(1, std::memory_order_release);
}

void TimestampCache::get_datetime(char out[24]) const
{
    maybe_update();
    unsigned s1, s2;
    do
    {
        do
        {
            s1 = seq_.load(std::memory_order_acquire);
        } while (s1 & 1);
        std::memcpy(out, datetime_str_, 24);
        s2 = seq_.load(std::memory_order_acquire);
    } while (s1 != s2);
}

void TimestampCache::get_date(char out[12]) const
{
    maybe_update();
    unsigned s1, s2;
    do
    {
        do
        {
            s1 = seq_.load(std::memory_order_acquire);
        } while (s1 & 1);
        std::memcpy(out, date_str_, 12);
        s2 = seq_.load(std::memory_order_acquire);
    } while (s1 != s2);
}
