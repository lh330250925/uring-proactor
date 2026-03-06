#pragma once
#ifndef BUF_POOL_HPP
#include "buf_pool.hpp"
#endif
template<unsigned int N>
BufPool<N>::BufPool()
{
    for (int i = 0; i < N; ++i)
    {
        buf_size_[i] = 0;
    }
    for (int i = 0; i < N; ++i)
    {
        release(i);
    }
}
template<unsigned int N>
unsigned int BufPool<N>::acquire(){
    if(head_ == tail_)
    {
        return -1; 
    }
    return free_queue_[head_++ & mask_];
}
template<unsigned int N>
void BufPool<N>::release(unsigned int idx){
    free_queue_[tail_++ & mask_] = idx;
    buf_size_[idx] = 0;
}