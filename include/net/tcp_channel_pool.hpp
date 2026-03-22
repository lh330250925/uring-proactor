#pragma once
#include <cassert>
#include <new>
#include "net/channel.hpp"
#include "util/noncopyable.hpp"

class TcpThread;

class TcpChannelPool : NonCopyable
{
public:
    explicit TcpChannelPool(TcpThread &thread);
    ~TcpChannelPool();
    TcpThread &thread() { return thread_; }
    TcpChannel *acquire(int fd);
    void release(TcpChannel *ch);
    TcpChannel *channel(int idx) const { return &channels_[idx]; }
    int index(const TcpChannel *ch) const { return ch - channels_; }
    int size() const { return pool_size_; }

private:
    TcpThread &thread_;
    TcpChannel *channels_;
    int *free_queue_;
    int head_ = 0;
    int tail_ = 0;
    int pool_size_;
    int mask_;
};
