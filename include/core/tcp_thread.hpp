#pragma once
#include <vector>
#include "core/io_thread.hpp"
#include "net/server.hpp"
#include "net/tcp_channel_pool.hpp"

class TcpThread : public IoThread
{
    TcpServer &server_;
    std::optional<TcpChannelPool> tcp_channel_pool_;
    std::vector<TcpChannel *> pending_reads_;
    std::coroutine_handle<> accept_handle_;
    void do_init_resources() override;
    void on_loop_start() override;
    void on_batch_done() override;
    void on_stop() override;

public:
    TcpThread(TcpServer &server, int cpu_id = -1);
    void mark_pending_read(TcpChannel *ch)
    {
        if (!ch->in_pending_reads_)
        {
            ch->in_pending_reads_ = true;
            pending_reads_.push_back(ch);
        }
    }
    TcpChannelPool &get_tcp_channel_pool() { return *tcp_channel_pool_; }
    TcpServer &get_server() { return server_; }
};