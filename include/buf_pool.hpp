#pragma once
#include <liburing.h>
#include <cassert>
#include "log.hpp"
#include "noncopyable.hpp"
template <unsigned int N>
class BufPool : NonCopyable
{
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    BufPool();
    ~BufPool() = default;
    unsigned int get_size(const unsigned int &idx) const
    {
        return buf_size_[idx];
    }
    unsigned int set_size(const unsigned int &idx, unsigned int size)
    {
        return buf_size_[idx] = size;
    }
    unsigned int get_idx(const char *buf) const
    {
        return (buf - bufs_) / buf_size;
    }
    char *get_addr(const unsigned int &idx) const
    {
        return bufs_ + idx * buf_size;
    }
    unsigned int acquire();
    void release(unsigned int idx);

private:
    const static int buf_size = 4096;
    char bufs_[N * buf_size];
    unsigned int buf_size_[N];
    const int size_ = N;
    unsigned int free_queue_[N];
    unsigned int head_ = 0;
    unsigned int tail_ = 0;
    unsigned int mask_ = N - 1;
};
#include "buf_pool.ipp"