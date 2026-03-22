#pragma once
#include <optional>
#include <sys/uio.h>
#include "core/buf_ring.hpp"
#include "buffer/buf_pool.hpp"
#include "buffer/msghdr_pool.hpp"
#include "buffer/dgram_msg.hpp"

class ReadBufBase : NonCopyable
{
public:
    struct PeekResult
    {
        char **data;
        unsigned int *size;
        unsigned int count;
    };

protected:
    unsigned int *buf_queue_;
    BufRing *buf_ring_;
    unsigned int head_ = 0;
    unsigned int tail_ = 0;
    unsigned int readable_bytes_ = 0;
    int capacity_;
    unsigned int mask_;
    PeekResult peek_result_;

    explicit ReadBufBase(BufRing &buf_ring, int capacity);
    ~ReadBufBase();

public:
    bool push_buf(unsigned int idx);
    void set_buf_data_size(unsigned int idx, unsigned int size) { buf_ring_->set_data_size(idx, size); }
    int buf_group() const { return buf_ring_->group_id(); }
    bool empty() const { return head_ == tail_; }
    unsigned int readable_bytes() const { return readable_bytes_; }
    unsigned int readable_packets() const { return tail_ - head_; }
};

class ReadBufStream : public ReadBufBase
{
    unsigned int head_offset_ = 0;

public:
    explicit ReadBufStream(BufRing &buf_ring, int capacity);
    PeekResult *peek(unsigned int size);
    bool consume(unsigned int size);
    void reset()
    {
        consume(readable_bytes());
        head_offset_ = 0;
        head_ = tail_ = 0;
    }
};

class ReadBufDgram : public ReadBufBase
{
    msghdr recv_msghdr_;
    std::optional<DgramMsg> peeked_msg_;

public:
    explicit ReadBufDgram(BufRing &buf_ring, int capacity);
    msghdr *recv_msghdr() { return &recv_msghdr_; }
    DgramMsg *peek();
    bool consume();
};

class WriteBufBase : NonCopyable
{
protected:
    unsigned int *pending_bufs_;
    unsigned int *send_queue_;
    int send_head_ = 0;
    int pending_head_ = 0;
    int pending_tail_ = 0;
    int send_tail_ = 0;
    BufPool *buf_pool_;
    int capacity_;
    unsigned int mask_;

    explicit WriteBufBase(BufPool &buf_pool, int capacity);
    ~WriteBufBase();

public:
    bool append(const char *data, unsigned int size);
    bool prepend(const char *data, unsigned int size);
};

class WriteBufStream : public WriteBufBase
{
    iovec *iovec_;
    int release_guard_ = 0;

public:
    explicit WriteBufStream(BufPool &buf_pool, int capacity);
    ~WriteBufStream();
    bool submit();
    const iovec *peek_iovec(int &count) const;
    void release();
    void set_release_guard(int count) { release_guard_ = send_head_ + count; }
    void reset()
    {
        while (send_head_ != send_tail_)
            buf_pool_->release(send_queue_[send_head_++ & mask_]);
        while (pending_head_ != pending_tail_)
            buf_pool_->release(pending_bufs_[pending_head_++ & mask_]);
        pending_head_ = pending_tail_ = send_head_ = send_tail_ = 0;
    }
};

class WriteBufDgram : public WriteBufBase
{
    MsghdrSlot **slot_queue_;
    int msghdr_head_ = 0;
    int msghdr_tail_ = 0;
    MsghdrPool *msghdr_pool_;
    int max_iov_;

public:
    explicit WriteBufDgram(BufPool &buf_pool, MsghdrPool &msghdr_pool, int capacity);
    ~WriteBufDgram();
    MsghdrSlot *acquire_slot() { return msghdr_pool_->acquire(); }
    bool submit(MsghdrSlot *slot);
    MsghdrSlot *peek_slot(int offset = 0) const;
    void release_slot(MsghdrSlot *slot);
    void clear_slot_queue();
    int slot_queue_size() const { return msghdr_tail_ - msghdr_head_; }
};
