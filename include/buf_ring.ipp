#pragma once
#ifndef BUF_RING_HPP
#include "buf_ring.hpp"
#endif
template <unsigned int N>
void BufRing<N>::add(const char *buf, int offset)
{
    io_uring_buf_ring_add(br_, (void *)buf, buf_size, (buf - bufs_) / buf_size, mask_, offset);
}
template <unsigned int N>
void BufRing<N>::advance(unsigned nr)
{
    io_uring_buf_ring_advance(br_, nr);
}
template <unsigned int N>
BufRing<N>::BufRing(IoRing &io_ring) : io_ring_(io_ring), br_id_(BGID++), err_(0), br_(io_uring_setup_buf_ring(&io_ring_.get_ring(), N, br_id_, 0, &err_))
{
    for (int i = 0; i < size_; ++i)
    {
        add(bufs_ + i * buf_size, i);
    }
    advance(size_);
    for (int i = 0; i < size_; ++i)
    {
        buf_size_[i] = 0;
    }
}
template <unsigned int N>
BufRing<N>::~BufRing()
{
    io_uring_free_buf_ring(&io_ring_.get_ring(), br_, size_, br_id_);
}
template <unsigned int N>
void BufRing<N>::release(const unsigned int &idx)
{
    add(bufs_ + idx * buf_size);
    advance();
    buf_size_[idx] = 0;
}