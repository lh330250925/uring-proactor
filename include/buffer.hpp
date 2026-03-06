#pragma once
#include <vector>
#include <cassert>
#include "log.hpp"
#include "buf_ring.hpp"
#include "buf_pool.hpp"
template <unsigned int N>
class ReadBuf : NonCopyable
{
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    unsigned int buf_queue_[N];
    BufRing *buf_ring_;
    unsigned int head_block_ = 0;
    unsigned int tail_block_ = 0;
    unsigned int head_offset_ = 0;
    static constexpr unsigned int block_mask_ = N - 1;
    unsigned int readable_bytes_ = 0;

public:
    struct result
    {
        char *data[N];
        unsigned int size[N];
        unsigned int count;
    };
    ReadBuf(BufRing &buf_ring) : buf_ring_(&buf_ring)
    {
    }
    bool append(unsigned int block_idx)
    {
        if (block_idx >= N || tail_block_ - head_block_ >= N)
        {
            return false;
        }
        buf_queue_[tail_block_++ & block_mask_] = block_idx;
        readable_bytes_ += buf_ring_.get_size(block_idx);
        return true;
    };
    bool empty() const
    {
        return head_block_ == tail_block_;
    }
    unsigned int readable_bytes() const
    {
        return readable_bytes_;
    }
    result *peek(unsigned int size);
    bool consume(unsigned int size);
private:
    result result_;
};
template <unsigned int N>
class WriteBuf : NonCopyable
{
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    unsigned int buf_queue_[N];
    unsigned int submit_queue_[N];
    int buf_head_ = 0;
    int buf_tail_ = 0;
    int submit_head_ = 0;
    int submit_tail_ = 0;
    BufPool *buf_pool_;
    static constexpr unsigned int mask_ = N - 1;

public:
    WriteBuf(BufPool &buf_pool) : buf_pool_(&buf_pool) {}
    bool append(const char *data, unsigned int size);
    bool prepend(const char *data, unsigned int size);
    bool submit();
};

#include "buffer.ipp"