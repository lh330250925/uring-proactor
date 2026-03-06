#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
template<typename T, size_t Capacity>
class MPSCQueue
{
static_assert((Capacity&(Capacity-1)) == 0, "Capacity must be a power of 2");
private:
    struct Slot
    {
        std::atomic<size_t> sequence;
        T data;
    };
    
    alignas(std::hardware_destructive_interference_size) size_t head_;
    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> tail_;
    Slot buffer_[Capacity];

public:
    MPSCQueue(); 
    ~MPSCQueue();
    bool enqueue(const T&);
    bool dequeue(T&);
};

template<typename T, size_t Capacity>
MPSCQueue<T, Capacity>::MPSCQueue() : head_(0), tail_(0)
{
    for (size_t i = 0; i < Capacity; ++i)
    {
        buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
}

template<typename T, size_t Capacity>
MPSCQueue<T, Capacity>::~MPSCQueue() = default;

template<typename T, size_t Capacity>
bool MPSCQueue<T, Capacity>::enqueue(const T &item)
{
    size_t pos;
    Slot *slot;
    while (true)
    {
        pos = tail_.load(std::memory_order_relaxed);
        slot = &buffer_[pos & (Capacity - 1)];
        intptr_t diff = static_cast<intptr_t>(slot->sequence.load(std::memory_order_acquire)) - static_cast<intptr_t>(pos);
        if (diff == 0)
        {
            tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed);
            break;
        }
        else if (diff < 0)
        {
            return false;
        }
    }
    slot->data = item;
    slot->sequence.store(pos + 1, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
bool MPSCQueue<T, Capacity>::dequeue(T &item)
{
    Slot &slot = buffer_[head_ & (Capacity - 1)];
    size_t seq = slot.sequence.load(std::memory_order_acquire);
    intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(head_ + 1);
    if (diff == 0)
    {
        item = slot.data;
        slot.sequence.store(head_ + Capacity, std::memory_order_release);
        head_++;
        return true;
    }
    return false;
}