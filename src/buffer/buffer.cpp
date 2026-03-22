#include "buffer/buffer.hpp"
#include <algorithm>
#include <netinet/in.h>
ReadBufBase::ReadBufBase(BufRing &buf_ring, int capacity)
    : buf_queue_(new unsigned int[capacity]),
      buf_ring_(&buf_ring),
      capacity_(capacity),
      mask_(capacity - 1)
{
    peek_result_.data = new char *[capacity];
    peek_result_.size = new unsigned int[capacity];
}
ReadBufBase::~ReadBufBase()
{
    delete[] buf_queue_;
    delete[] peek_result_.data;
    delete[] peek_result_.size;
}
bool ReadBufBase::push_buf(unsigned int idx)
{
    if (tail_ - head_ >= (unsigned int)capacity_)
        return false;
    buf_queue_[tail_++ & mask_] = idx;
    readable_bytes_ += buf_ring_->data_size(idx);
    return true;
}
ReadBufStream::ReadBufStream(BufRing &buf_ring, int capacity)
    : ReadBufBase(buf_ring, capacity) {}

ReadBufBase::PeekResult *ReadBufStream::peek(unsigned int size)
{
    if (size > readable_bytes_)
        return nullptr;
    unsigned int idx = 0;
    for (auto i = head_; i < tail_ && size > 0; ++i)
    {
        unsigned int offset = (i == head_) ? head_offset_ : 0;
        unsigned int block_readable = buf_ring_->data_size(buf_queue_[i & mask_]) - offset;
        char *start_ptr = buf_ring_->buf_addr(buf_queue_[i & mask_]) + offset;
        if (block_readable > size)
            block_readable = size;
        peek_result_.data[idx] = start_ptr;
        peek_result_.size[idx] = block_readable;
        size -= block_readable;
        ++idx;
    }
    peek_result_.count = idx;
    return &peek_result_;
}
bool ReadBufStream::consume(unsigned int size)
{
    if (size > readable_bytes_)
        return false;
    readable_bytes_ -= size;
    while (size > 0)
    {
        unsigned int avail = buf_ring_->data_size(buf_queue_[head_ & mask_]) - head_offset_;
        if (avail > size)
        {
            head_offset_ += size;
            break;
        }
        size -= avail;
        buf_ring_->release(buf_queue_[head_ & mask_]);
        head_offset_ = 0;
        ++head_;
    }
    return true;
}
ReadBufDgram::ReadBufDgram(BufRing &buf_ring, int capacity)
    : ReadBufBase(buf_ring, capacity), recv_msghdr_{}
{
    recv_msghdr_.msg_namelen = sizeof(sockaddr_in);
    recv_msghdr_.msg_controllen = 0;
}

DgramMsg *ReadBufDgram::peek()
{
    if (empty())
        return nullptr;
    peeked_msg_.emplace(
        buf_ring_->buf_addr(buf_queue_[head_ & mask_]),
        buf_ring_->data_size(buf_queue_[head_ & mask_]),
        &recv_msghdr_);
    return &peeked_msg_.value();
}
bool ReadBufDgram::consume()
{
    if (empty())
        return false;
    readable_bytes_ -= buf_ring_->data_size(buf_queue_[head_ & mask_]);
    buf_ring_->release(buf_queue_[head_ & mask_]);
    ++head_;
    return true;
}
WriteBufBase::WriteBufBase(BufPool &buf_pool, int capacity)
    : pending_bufs_(new unsigned int[capacity]),
      send_queue_(new unsigned int[capacity]),
      buf_pool_(&buf_pool),
      capacity_(capacity),
      mask_(capacity - 1) {}

WriteBufBase::~WriteBufBase()
{
    delete[] pending_bufs_;
    delete[] send_queue_;
}
bool WriteBufBase::append(const char *data, unsigned int size)
{
    const int original_tail = pending_tail_;
    const bool was_empty = (pending_head_ == pending_tail_);
    unsigned int original_tail_size = 0;
    if (was_empty)
    {
        auto idx = buf_pool_->acquire();
        if (idx == BufPool::invalid_idx)
            return false;
        pending_bufs_[pending_tail_++ & mask_] = idx;
    }
    else
    {
        original_tail_size = buf_pool_->data_size(pending_bufs_[(pending_tail_ - 1) & mask_]);
    }
    while (size > 0)
    {
        unsigned int current_size = buf_pool_->data_size(pending_bufs_[(pending_tail_ - 1) & mask_]);
        unsigned int writable = buf_pool_->buf_size() - current_size;
        unsigned int to_copy = std::min(writable, size);
        if (to_copy > 0)
        {
            std::copy(data, data + to_copy,
                      buf_pool_->buf_addr(pending_bufs_[(pending_tail_ - 1) & mask_]) + current_size);
            buf_pool_->set_data_size(pending_bufs_[(pending_tail_ - 1) & mask_], current_size + to_copy);
            data += to_copy;
            size -= to_copy;
        }
        if (size > 0)
        {
            auto idx = buf_pool_->acquire();
            if (idx == BufPool::invalid_idx)
            {
                if (!was_empty)
                    buf_pool_->set_data_size(pending_bufs_[(original_tail - 1) & mask_], original_tail_size);
                while (pending_tail_ > original_tail)
                    buf_pool_->release(pending_bufs_[--pending_tail_ & mask_]);
                return false;
            }
            pending_bufs_[pending_tail_++ & mask_] = idx;
        }
    }
    return true;
}
bool WriteBufBase::prepend(const char *data, unsigned int size)
{
    unsigned int block_to_write = (size + buf_pool_->buf_size() - 1) / buf_pool_->buf_size();
    int new_head = pending_head_ - block_to_write;
    for (int i = new_head; i < pending_head_; ++i)
    {
        auto idx = buf_pool_->acquire();
        if (idx == BufPool::invalid_idx)
        {
            for (int j = new_head; j < i; ++j)
                buf_pool_->release(pending_bufs_[j & mask_]);
            return false;
        }
        pending_bufs_[i & mask_] = idx;
        unsigned int to_copy = std::min(size, static_cast<unsigned int>(buf_pool_->buf_size()));
        std::copy(data, data + to_copy, buf_pool_->buf_addr(idx));
        buf_pool_->set_data_size(idx, to_copy);
        data += to_copy;
        size -= to_copy;
    }
    pending_head_ = new_head;
    return true;
}
WriteBufStream::WriteBufStream(BufPool &buf_pool, int capacity)
    : WriteBufBase(buf_pool, capacity),
      iovec_(new iovec[capacity]) {}

WriteBufStream::~WriteBufStream()
{
    delete[] iovec_;
}

bool WriteBufStream::submit()
{
    if (pending_head_ == pending_tail_)
        return false;
    while (pending_head_ != pending_tail_)
        send_queue_[send_tail_++ & mask_] = pending_bufs_[pending_head_++ & mask_];
    return true;
}
const iovec *WriteBufStream::peek_iovec(int &count) const
{
    count = send_tail_ - send_head_;
    if (count <= 0)
    {
        count = 0;
        return nullptr;
    }
    for (int i = 0; i < count; ++i)
    {
        unsigned int idx = send_queue_[(send_head_ + i) & mask_];
        iovec_[i].iov_base = buf_pool_->buf_addr(idx);
        iovec_[i].iov_len = buf_pool_->data_size(idx);
    }
    return iovec_;
}
void WriteBufStream::release()
{
    if (release_guard_ <= send_head_)
        return;
    while (send_head_ != send_tail_ && send_head_ != release_guard_)
        buf_pool_->release(send_queue_[send_head_++ & mask_]);
}
WriteBufDgram::WriteBufDgram(BufPool &buf_pool, MsghdrPool &msghdr_pool, int capacity)
    : WriteBufBase(buf_pool, capacity),
      slot_queue_(new MsghdrSlot *[capacity]),
      msghdr_pool_(&msghdr_pool),
      max_iov_(msghdr_pool.max_iov()) {}

WriteBufDgram::~WriteBufDgram()
{
    delete[] slot_queue_;
}
bool WriteBufDgram::submit(MsghdrSlot *slot)
{
    if (pending_head_ == pending_tail_)
        return false;
    if (msghdr_tail_ - msghdr_head_ >= capacity_)
        return false;
    if (!slot)
        return false;
    int iov_count = 0;
    while (pending_head_ != pending_tail_)
    {
        unsigned int idx = pending_bufs_[pending_head_++ & mask_];
        if (iov_count < max_iov_)
        {
            slot->iov[iov_count].iov_base = buf_pool_->buf_addr(idx);
            slot->iov[iov_count].iov_len = buf_pool_->data_size(idx);
            ++iov_count;
        }
        else
            buf_pool_->release(idx);
    }
    slot->hdr.msg_iov = slot->iov;
    slot->hdr.msg_iovlen = iov_count;
    slot_queue_[msghdr_tail_++ & mask_] = slot;
    return true;
}
MsghdrSlot *WriteBufDgram::peek_slot(int offset) const
{
    if (msghdr_head_ + offset >= msghdr_tail_)
        return nullptr;
    return slot_queue_[(msghdr_head_ + offset) & mask_];
}
void WriteBufDgram::release_slot(MsghdrSlot *slot)
{
    for (size_t i = 0; i < slot->hdr.msg_iovlen; ++i)
        buf_pool_->release(static_cast<const char *>(slot->iov[i].iov_base));
    msghdr_pool_->release(slot);
}
void WriteBufDgram::clear_slot_queue()
{
    msghdr_head_ = msghdr_tail_;
}