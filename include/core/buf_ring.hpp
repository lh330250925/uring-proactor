#pragma once
#include <atomic>
#include <liburing.h>
#include <cassert>
#include "util/noncopyable.hpp"
#include "core/io_ring.hpp"

class BufRing : NonCopyable
{
public:
    BufRing(IoRing &io_ring, int buf_num, int buf_size = 4096);
    ~BufRing();
    int group_id() const { return group_id_; }
    int buf_num() const { return buf_num_; }
    int buf_size() const { return buf_size_; }
    unsigned int buf_index(const char *buf) const { return (buf - bufs_) / buf_size_; }
    char *buf_addr(unsigned int idx) const { return bufs_ + idx * buf_size_; }
    unsigned int data_size(unsigned int idx) const { return data_size_[idx]; }
    void set_data_size(unsigned int idx, unsigned int size) { data_size_[idx] = size; }
    void release(unsigned int idx);
    void release(const char *buf) { release(buf_index(buf)); }

private:
    static std::atomic<int> s_next_group_id_;
    const int group_id_;
    const int buf_size_;
    char *bufs_;
    int buf_num_;
    IoRing &io_ring_;
    io_uring_buf_ring *uring_ring_ = nullptr;
    int mask_;
    unsigned int *data_size_;
    void add_buf(const char *buf, int offset = 0);
    void advance_ring(unsigned nr = 1);
};