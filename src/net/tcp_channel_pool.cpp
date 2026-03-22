#include "net/tcp_channel_pool.hpp"
#include "core/tcp_thread.hpp"
#include <cassert>

TcpChannelPool::TcpChannelPool(TcpThread &thread)
    : thread_(thread),
      pool_size_(thread.get_server().get_pool_size()),
      mask_(thread.get_server().get_pool_size() - 1)
{
    int pool_size = thread.get_server().get_pool_size();
    assert(pool_size > 0 && (pool_size & (pool_size - 1)) == 0);
    channels_ = static_cast<TcpChannel *>(::operator new(sizeof(TcpChannel) * pool_size));
    free_queue_ = new int[pool_size];
    for (int i = 0; i < pool_size; ++i)
    {
        new (&channels_[i]) TcpChannel(-1, *this);
        free_queue_[tail_++ & mask_] = i;
    }
}
TcpChannelPool::~TcpChannelPool()
{
    for (int i = 0; i < pool_size_; ++i)
        channels_[i].~TcpChannel();
    ::operator delete(channels_);
    delete[] free_queue_;
}
TcpChannel *TcpChannelPool::acquire(int fd)
{
    if (head_ == tail_)
        return nullptr;
    int idx = free_queue_[head_++ & mask_];
    channels_[idx].assign(fd);
    return &channels_[idx];
}
void TcpChannelPool::release(TcpChannel *ch)
{
    assert(ch->is_closed());
    free_queue_[tail_++ & mask_] = index(ch);
}
