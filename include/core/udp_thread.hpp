#pragma once
#include <memory>
#include "core/io_thread.hpp"
#include "buffer/msghdr_pool.hpp"
#include "net/server.hpp"
#include "net/socket.hpp"

class UdpChannel;

class UdpThread : public IoThread
{
    UdpServer &server_;
    std::optional<MsghdrPool> msghdr_pool_;
    std::optional<Socket> socket_;
    std::unique_ptr<UdpChannel> channel_;

    void do_init_resources() override;

public:
    UdpThread(UdpServer &server, int cpu_id = -1);
    MsghdrPool &get_msghdr_pool() { return *msghdr_pool_; }
    UdpServer &get_server() { return server_; }
    UdpChannel &get_channel() { return *channel_; }
};
