#pragma once
#include <cassert>
#include "util/noncopyable.hpp"

class BufPool : NonCopyable
{
public:
    static constexpr unsigned int invalid_idx = ~0u;

    explicit BufPool(int buf_num, int buf_size = 4096);
    ~BufPool();
    int buf_size() const { return buf_size_; }
    unsigned int buf_index(const char *buf) const { return (buf - bufs_) / buf_size_; }
    char *buf_addr(unsigned int idx) const { return bufs_ + idx * buf_size_; }
    unsigned int data_size(unsigned int idx) const { return data_size_[idx]; }
    void set_data_size(unsigned int idx, unsigned int size) { data_size_[idx] = size; }
    unsigned int acquire();
    void release(unsigned int idx);
    void release(const char *buf) { release(buf_index(buf)); }

private:
    int buf_num_;
    int mask_;
    const int buf_size_;
    char *bufs_;
    unsigned int *data_size_;
    unsigned int *free_idx_queue_;
    unsigned int head_ = 0;
    unsigned int tail_ = 0;
};