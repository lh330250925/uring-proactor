#include "buffer/buf_pool.hpp"

BufPool::BufPool(int buf_num, int buf_size)
    : buf_num_(buf_num),
      mask_(buf_num - 1),
      buf_size_(buf_size),
      bufs_(new char[buf_num * buf_size_]),
      data_size_(new unsigned int[buf_num]),
      free_idx_queue_(new unsigned int[buf_num])
{
    assert(buf_num > 0 && (buf_num & (buf_num - 1)) == 0);
    for (int i = 0; i < buf_num_; ++i)
        data_size_[i] = 0;
    for (int i = 0; i < buf_num_; ++i)
        release(i);
}
BufPool::~BufPool()
{
    delete[] bufs_;
    delete[] data_size_;
    delete[] free_idx_queue_;
}
unsigned int BufPool::acquire()
{
    if (head_ == tail_)
        return invalid_idx;
    return free_idx_queue_[head_++ & mask_];
}
void BufPool::release(unsigned int idx)
{
    assert(tail_ - head_ < static_cast<unsigned int>(buf_num_));
    free_idx_queue_[tail_++ & mask_] = idx;
    data_size_[idx] = 0;
}