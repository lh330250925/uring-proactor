#include "core/tcp_thread.hpp"
#include "core/awaiter.hpp"
#include "net/acceptor.hpp"
#include <numa.h>
#include <sys/eventfd.h>
#include <vector>

TcpThread::TcpThread(TcpServer &server, int cpu_id)
    : IoThread(cpu_id), server_(server)
{
    start_thread();
}

void TcpThread::do_init_resources()
{
    io_uring_params *p_params = nullptr;
    io_uring_params params{};
    if (cpu_id_ >= 0)
    {
        numa_set_preferred(numa_node_of_cpu(cpu_id_));
        params = make_sqpoll_params(cpu_id_);
        p_params = &params;
    }
    io_ring_.emplace(server_.get_queue_depth(), p_params);
    buf_ring_.emplace(*io_ring_, server_.get_buf_ring_size());
    buf_pool_.emplace(server_.get_buf_pool_size());
    tcp_channel_pool_.emplace(*this);
    wakeup_fd_.emplace(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
}

void TcpThread::on_loop_start()
{
    accept_handle_ = accept_loop(*this).release();
    pending_reads_.reserve(tcp_channel_pool_->size());
}

void TcpThread::on_batch_done()
{
    for (TcpChannel *ch : pending_reads_)
    {
        ch->in_pending_reads_ = false;
        server_.on_read(*ch);
    }
    pending_reads_.clear();
}

void TcpThread::on_stop()
{
    IoRing &ring = *io_ring_;
    {
        auto sqe = ring.get_sqe();
        sqe.prep_cancel_all();
        sqe.set_data(nullptr);
    }
    std::vector<io_uring_cqe *> cqes(ring.get_queue_depth());
    for (bool done = false; !done;)
    {
        if (cpu_id_ == -1)
            ring.submit_and_wait(1);
        int n = ring.peek_batch_cqe(cqes.data(), static_cast<unsigned>(cqes.size()));
        for (int i = 0; i < n; ++i)
        {
            auto *token = static_cast<Token *>(io_uring_cqe_get_data(cqes[i]));
            if (token)
                token->complete(cqes[i]->res, cqes[i]->flags);
            else
                done = true;
        }
        ring.cqe_advance(n);
    }
    if (accept_handle_)
        accept_handle_.destroy();
}
