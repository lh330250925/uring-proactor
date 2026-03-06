#pragma once
#include <liburing.h>
#include <cstdlib>
#include <cassert>
#include "noncopyable.hpp"
#include "io_ring.hpp"
template <unsigned int N>
class BufRing : NonCopyable
{
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static constexpr int buf_size = 4096;
    static int BGID;
    const int br_id_;
    char bufs_[N * buf_size];
    constexpr static int size_ = N;
    IoRing &io_ring_;
    io_uring_buf_ring *br_;
    constexpr static int mask_ = N - 1;
    int err_;
    unsigned int buf_size_[N];
    void add(const char *buf, int offset = 0);
    void advance(unsigned nr = 1);

public:
    BufRing(IoRing &io_ring);
    ~BufRing();
    void release(const unsigned int &idx);
    char *get_addr(const unsigned int &idx) const
    {
        return (char *)bufs_ + idx * buf_size;
    }
    unsigned int get_size(const unsigned int &idx) const
    {
        return buf_size_[idx];
    }
    unsigned int get_idx(const char *buf) const
    {
        return (buf - bufs_) / buf_size;
    }
};
#include "buf_ring.ipp"