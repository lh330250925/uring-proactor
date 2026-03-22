#pragma once
#include <memory>
#include <thread>
#include <vector>
#include "util/noncopyable.hpp"
#include <cassert>

class TcpThread;
class UdpThread;

class Server : NonCopyable
{
protected:
    int port_;
    int thread_nums_;
    int buf_pool_size_;
    bool cpu_affinity_;

public:
    Server(int port, int thread_nums, int buf_pool_size, bool cpu_affinity = false)
        : port_(port), thread_nums_(thread_nums), buf_pool_size_(buf_pool_size), cpu_affinity_(cpu_affinity)
    {
        assert(thread_nums > 0 && thread_nums <= std::thread::hardware_concurrency());
        if (cpu_affinity_)
        {
            assert(thread_nums <= std::thread::hardware_concurrency() / 2);
        }
    }
    virtual ~Server() = default;
    int get_port() const { return port_; }
    int get_thread_nums() const { return thread_nums_; }
    int get_buf_pool_size() const { return buf_pool_size_; }
    virtual void start() = 0;
};
class TcpChannel;

class TcpServer : public Server
{
    int channel_capacity_;
    int buf_ring_size_;
    int pool_size_;
    unsigned queue_depth_;

public:
    TcpServer(int port, int thread_nums, int buf_pool_size, int channel_capacity, int buf_ring_size, int pool_size, unsigned queue_depth = 256, bool cpu_affinity = false);
    int get_channel_capacity() const { return channel_capacity_; }
    int get_buf_ring_size() const { return buf_ring_size_; }
    int get_pool_size() const { return pool_size_; }

protected:
    std::vector<std::unique_ptr<TcpThread>> threads_;

public:
    unsigned get_queue_depth() const { return queue_depth_; }
    virtual void on_read(TcpChannel &ch) = 0;
    virtual void on_write(TcpChannel &ch, int res) {}
    virtual void on_accept(TcpChannel &ch) {}
    virtual void on_close(TcpChannel &ch, int res) {}
    virtual ~TcpServer();
    void start() override;
};
class UdpChannel;

class UdpServer : public Server
{
    int buf_ring_size_;
    int msghdr_pool_size_;
    int channel_capacity_;
    unsigned queue_depth_;

protected:
    std::vector<std::unique_ptr<UdpThread>> threads_;

public:
    UdpServer(int port, int thread_nums, int buf_pool_size, int buf_ring_size,
              int msghdr_pool_size, int channel_capacity,
              unsigned queue_depth = 256, bool cpu_affinity = false);
    int get_buf_ring_size() const { return buf_ring_size_; }
    int get_msghdr_pool_size() const { return msghdr_pool_size_; }
    int get_channel_capacity() const { return channel_capacity_; }
    unsigned get_queue_depth() const { return queue_depth_; }
    virtual void on_read(UdpChannel &) = 0;
    virtual void on_write(UdpChannel &, int) {}
    ~UdpServer();
    void start() override;
};