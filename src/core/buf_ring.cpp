#include "core/buf_ring.hpp"

std::atomic<int> BufRing::s_next_group_id_ = 0;

void BufRing::add_buf(const char *buf, int offset)
{
    io_uring_buf_ring_add(uring_ring_, (void *)buf, buf_size_, buf_index(buf), mask_, offset);
}
void BufRing::advance_ring(unsigned nr)
{
    io_uring_buf_ring_advance(uring_ring_, nr);
}
BufRing::BufRing(IoRing &io_ring, int buf_num, int buf_size)
    : group_id_(s_next_group_id_.fetch_add(1, std::memory_order_relaxed)),
      buf_size_(buf_size),
      bufs_(new char[buf_num * buf_size_]),
      buf_num_(buf_num),
      io_ring_(io_ring),
      mask_(buf_num - 1),
      data_size_(new unsigned int[buf_num]{})
{
    assert(buf_num > 0 && (buf_num & (buf_num - 1)) == 0);
    int setup_err = 0;
    uring_ring_ = io_uring_setup_buf_ring(&io_ring_.get_ring(), buf_num, group_id_, 0, &setup_err);
    assert(uring_ring_ != nullptr);
    for (int i = 0; i < buf_num_; ++i)
        add_buf(bufs_ + i * buf_size_, i);
    advance_ring(buf_num_);
}
BufRing::~BufRing()
{
    if (uring_ring_)
        io_uring_free_buf_ring(&io_ring_.get_ring(), uring_ring_, buf_num_, group_id_);
    delete[] bufs_;
    delete[] data_size_;
}
void BufRing::release(unsigned int idx)
{
    add_buf(bufs_ + idx * buf_size_);
    advance_ring();
    data_size_[idx] = 0;
}