#include "buffer/msghdr_pool.hpp"
#include <cstring>
MsghdrPool::MsghdrPool(int capacity, int max_iov)
    : capacity_(capacity),
      mask_(capacity - 1),
      max_iov_(max_iov),
      slots_(new MsghdrSlot[capacity]),
      iovecs_(new iovec[capacity * max_iov]),
      pool_(new MsghdrSlot *[capacity])
{
    assert(capacity > 0 && (capacity & (capacity - 1)) == 0);
    assert(max_iov > 0);
    std::memset(iovecs_, 0, sizeof(iovec) * capacity * max_iov);
    for (int i = 0; i < capacity_; ++i)
    {
        std::memset(&slots_[i].hdr, 0, sizeof(msghdr));
        std::memset(&slots_[i].addr, 0, sizeof(sockaddr_storage));
        slots_[i].hdr.msg_name = &slots_[i].addr;
        slots_[i].hdr.msg_namelen = sizeof(sockaddr_storage);
        slots_[i].iov = &iovecs_[i * max_iov_];
        pool_[i] = &slots_[i];
    }
    tail_ = capacity_;
}
MsghdrPool::~MsghdrPool()
{
    delete[] slots_;
    delete[] iovecs_;
    delete[] pool_;
}
MsghdrSlot *MsghdrPool::acquire()
{
    if (head_ == tail_)
        return nullptr;
    return pool_[head_++ & mask_];
}
void MsghdrPool::release(MsghdrSlot *slot)
{
    assert(tail_ - head_ < static_cast<unsigned int>(capacity_));
    std::memset(&slot->hdr, 0, sizeof(msghdr));
    std::memset(&slot->addr, 0, sizeof(sockaddr_storage));
    slot->hdr.msg_name = &slot->addr;
    slot->hdr.msg_namelen = sizeof(sockaddr_storage);
    std::memset(slot->iov, 0, sizeof(iovec) * max_iov_);
    pool_[tail_++ & mask_] = slot;
}
