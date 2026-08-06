#include "core/udp_thread.hpp"
#include "net/channel.hpp"
#include <numa.h>
#include <sys/eventfd.h>

UdpThread::UdpThread(UdpServer &server, int cpu_id)
    : IoThread(cpu_id), server_(server)
{
    start_thread();
}

void UdpThread::do_init_resources()
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
    msghdr_pool_.emplace(server_.get_msghdr_pool_size());
    socket_.emplace(static_cast<uint16_t>(server_.get_port()), Socket::Type::UDP);
    channel_ = std::make_unique<UdpChannel>(*this);
    channel_->assign(socket_->fd());
    wakeup_fd_.emplace(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
}
