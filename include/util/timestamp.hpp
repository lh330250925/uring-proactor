#pragma once
#include <atomic>
#include <ctime>
#include <cstring>

class TimestampCache
{
public:
    void get_datetime(char out[24]) const;

    void get_date(char out[12]) const;

private:
    void maybe_update() const;

    mutable std::atomic<time_t> cached_second_{0};
    mutable std::atomic<unsigned> seq_{0};
    mutable char datetime_str_[24]{};
    mutable char date_str_[12]{};
};
