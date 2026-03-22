#pragma once
#include <cassert>
#include <sys/socket.h>
#include <sys/uio.h>
#include "util/noncopyable.hpp"
#include "core/awaiter.hpp"

struct MsghdrSlot
{
    MsgToken token_;
    msghdr hdr;
    sockaddr_storage addr;
    iovec *iov;
};

class MsghdrPool : NonCopyable
{
    int capacity_;
    int mask_;
    int max_iov_;
    MsghdrSlot *slots_;
    iovec *iovecs_;
    MsghdrSlot **pool_;
    unsigned int head_ = 0;
    unsigned int tail_ = 0;

public:
    explicit MsghdrPool(int capacity, int max_iov = 16);
    ~MsghdrPool();
    int max_iov() const { return max_iov_; }
    MsghdrSlot *acquire();
    void release(MsghdrSlot *slot);
};