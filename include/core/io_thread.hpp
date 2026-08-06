#pragma once
#include <thread>
#include <optional>
#include <semaphore>
#include <liburing.h>
#include "core/io_ring.hpp"
#include "core/buf_ring.hpp"
#include "buffer/buf_pool.hpp"
#include "util/noncopyable.hpp"

class IoThread : NonCopyable
{
protected:
    int cpu_id_;
    std::optional<IoRing> io_ring_;
    std::optional<BufRing> buf_ring_;
    std::optional<BufPool> buf_pool_;
    std::optional<int> wakeup_fd_;
    uint64_t wakeup_buf_ = 0;
    std::binary_semaphore ready_{0};
    std::jthread thread_;

    static io_uring_params make_sqpoll_params(int cpu_id);

    void bind_to_cpu();
    void start_thread();
    void run_loop(std::stop_token st);

    virtual void do_init_resources() = 0;
    virtual void on_loop_start() {}
    virtual void on_batch_done() {}
    virtual void on_stop();

public:
    explicit IoThread(int cpu_id) : cpu_id_(cpu_id) {}
    virtual ~IoThread();

    IoRing &get_ring() { return *io_ring_; }
    BufRing &get_buf_ring() { return *buf_ring_; }
    BufPool &get_buf_pool() { return *buf_pool_; }
};
