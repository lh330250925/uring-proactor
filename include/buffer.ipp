#pragma once
#include <algorithm>
#ifndef BUFFER_HPP
#include "buffer.hpp"
#endif

template <unsigned int N>
ReadBuf<N>::result *ReadBuf<N>::peek(unsigned int size)
{
    if (size > readable_bytes_)
    {
        return nullptr;
    }
    unsigned int idx = 0;
    for (auto i = head_block_; i < tail_block_ && size > 0; ++i)
    {
        unsigned int offset = (i == head_block_) ? head_offset_ : 0;
        unsigned int block_readable = buf_ring_->get_size(buf_queue_[i & block_mask_]) - offset;
        char *start_ptr = buf_ring_->get_addr(buf_queue_[i & block_mask_]) + offset;
        if (block_readable > size)
        {
            block_readable = size;
        }
        result_.data[idx] = start_ptr;
        result_.size[idx] = block_readable;
        size -= block_readable;
        ++idx;
    }
    result_.count = idx;
    return &result_;
}

template <unsigned int N>
bool ReadBuf<N>::consume(unsigned int size)
{
    if (size > readable_bytes_)
    {
        return false;
    }
    readable_bytes_ -= size;
    while (size > 0)
    {
        unsigned int avail = buf_ring_->get_size(buf_queue_[head_block_ & block_mask_]) - head_offset_;
        if (avail > size)
        {
            head_offset_ += size;
            break;
        }
        size -= avail;
        buf_ring_->release(buf_queue_[head_block_ & block_mask_]);
        head_offset_ = 0;
        ++head_block_;
    }
    return true;
}

template <unsigned int N>
bool WriteBuf<N>::append(const char *data, unsigned int size)
{
    const int original_tail = buf_tail_;
    const bool was_empty = (buf_head_ == buf_tail_);
    unsigned int original_tail_size = 0;
    if (was_empty)
    {
        auto idx = buf_pool_->acquire();
        if (idx == (unsigned int)-1)
        {
            return false;
        }
        buf_queue_[buf_tail_++ & mask_] = idx;
    }
    else
    {
        original_tail_size = buf_pool_->get_size(buf_queue_[(buf_tail_ - 1) & mask_]);
    }
    while (size > 0)
    {
        unsigned int current_size = buf_pool_->get_size(buf_queue_[(buf_tail_ - 1) & mask_]);
        unsigned int writable = 4096 - current_size;
        unsigned int to_copy = std::min(writable, size);
        if (to_copy > 0)
        {
            std::copy(data, data + to_copy,
                      buf_pool_->get_addr(buf_queue_[(buf_tail_ - 1) & mask_]) + current_size);
            buf_pool_->set_size(buf_queue_[(buf_tail_ - 1) & mask_], current_size + to_copy);
            data += to_copy;
            size -= to_copy;
        }
        if (size > 0)
        {
            auto idx = buf_pool_->acquire();
            if (idx == (unsigned int)-1)
            {
                if (!was_empty)
                {
                    buf_pool_->set_size(buf_queue_[(original_tail - 1) & mask_], original_tail_size);
                }
                while (buf_tail_ > original_tail)
                {
                    buf_pool_->release(buf_queue_[--buf_tail_ & mask_]);
                }
                return false;
            }
            buf_queue_[buf_tail_++ & mask_] = idx;
        }
    }
    return true;
}

template <unsigned int N>
bool WriteBuf<N>::prepend(const char *data, unsigned int size)
{
    unsigned int block_to_write = (size + 4095) >> 12;
    int new_head = buf_head_ - block_to_write;
    for (int i = new_head; i < buf_head_; ++i)
    {
        auto idx = buf_pool_->acquire();
        if (idx == (unsigned int)-1)
        {
            for (int j = new_head; j < i; ++j)
            {
                buf_pool_->release(buf_queue_[j & mask_]);
            }
            return false;
        }
        buf_queue_[i & mask_] = idx;
        unsigned int to_copy = std::min(size, 4096u);
        std::copy(data, data + to_copy, buf_pool_->get_addr(idx));
        buf_pool_->set_size(idx, to_copy);
        data += to_copy;
        size -= to_copy;
    }
    buf_head_ = new_head;
    return true;
}

template <unsigned int N>
bool WriteBuf<N>::submit()
{
    if (buf_head_ == buf_tail_)
    {
        return false;
    }
    while (buf_head_ != buf_tail_)
    {
        submit_queue_[submit_tail_++ & mask_] = buf_queue_[buf_head_++ & mask_];
    }
    return true;
}
