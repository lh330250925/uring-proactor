#include "core/io_thread.hpp"
#include "core/awaiter.hpp"
#include <climits>
#include <sched.h>
#include <numa.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

io_uring_params IoThread::make_sqpoll_params(int cpu_id)
{
    io_uring_params params{};
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
    params.sq_thread_cpu = static_cast<unsigned>(cpu_id) + 1;
    params.sq_thread_idle = UINT32_MAX;
    return params;
}

void IoThread::bind_to_cpu()
{
    if (cpu_id_ < 0)
        return;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id_, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

void IoThread::start_thread()
{
    if (cpu_id_ >= 0)
    {
        thread_ = std::jthread([this](std::stop_token st)
                               { bind_to_cpu(); do_init_resources(); ready_.release(); run_loop(st); });
        ready_.acquire();
    }
    else
    {
        do_init_resources();
        thread_ = std::jthread([this](std::stop_token st)
                               { run_loop(st); });
    }
}

void IoThread::run_loop(std::stop_token st)
{
    on_loop_start();
    IoRing &ring = *io_ring_;
    std::vector<io_uring_cqe *> cqes(ring.get_queue_depth());
    if (wakeup_fd_)
    {
        auto sqe = ring.get_sqe();
        sqe.prep_read(*wakeup_fd_, &wakeup_buf_, sizeof(wakeup_buf_), 0);
        sqe.set_data(reinterpret_cast<void *>(-1));
    }
    const std::stop_callback cb(st, [this]
                                {
        if (wakeup_fd_)
        {
            uint64_t one = 1;
            ::write(*wakeup_fd_, &one, sizeof(one));
        } });
    while (!st.stop_requested())
    {
        if (cpu_id_ == -1)
            ring.submit_and_wait(1);
        else
            ring.submit();
        int n = ring.peek_batch_cqe(cqes.data(), static_cast<unsigned>(cqes.size()));
        for (int i = 0; i < n; ++i)
        {
            auto *token = static_cast<Token *>(io_uring_cqe_get_data(cqes[i]));
            if (token && token != reinterpret_cast<Token *>(-1))
                token->complete(cqes[i]->res, cqes[i]->flags);
        }
        ring.cqe_advance(n);
        on_batch_done();
    }
    on_stop();
}

void IoThread::on_stop()
{
    auto sqe = io_ring_->get_sqe();
    sqe.prep_cancel_all();
    sqe.set_data(nullptr);
    io_ring_->submit();
}

IoThread::~IoThread()
{
    if (wakeup_fd_)
        ::close(*wakeup_fd_);
}
