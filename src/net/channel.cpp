#include "net/channel.hpp"
#include "net/tcp_channel_pool.hpp"
#include "net/handler.hpp"
#include "core/tcp_thread.hpp"
#include "core/udp_thread.hpp"

TcpChannel::TcpChannel(int fd, TcpChannelPool &pool)
    : Channel(fd, pool.thread().get_ring()),
      read_buf_(pool.thread().get_buf_ring(), pool.thread().get_server().get_channel_capacity()),
      write_buf_(pool.thread().get_buf_pool(), pool.thread().get_server().get_channel_capacity()),
      pool_(pool),
      server_(pool.thread().get_server())
{
}

void TcpChannel::on_drained()
{
    if (is_closing())
    {
        notify_token_.notify();
        pool_.release(this);
    }
}

void TcpChannel::close()
{
    notify_token_.notify_close();
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
}

TcpThread &TcpChannel::thread() { return pool_.thread(); }

void TcpChannel::assign(int fd)
{
    if (read_handle_ && read_handle_.done())
    {
        read_handle_.destroy();
        read_handle_ = {};
    }
    if (write_handle_ && write_handle_.done())
    {
        write_handle_.destroy();
        write_handle_ = {};
    }
    fd_ = fd;
    notify_token_ = NotifyToken{};
    read_handle_ = tcp_read_loop(*this).release();
    write_handle_ = tcp_write_loop(*this).release();
    server_.on_accept(*this);
}

UdpChannel::UdpChannel(UdpThread &thread)
    : Channel(-1, thread.get_ring()),
      read_buf_(thread.get_buf_ring(), thread.get_server().get_channel_capacity()),
      write_buf_(thread.get_buf_pool(), thread.get_msghdr_pool(), thread.get_server().get_channel_capacity()),
      thread_(thread)
{
}

void UdpChannel::assign(int fd)
{
    fd_ = fd;
    read_handle_ = udp_read_loop(*this).release();
    write_handle_ = udp_write_loop(*this).release();
}

UdpServer &UdpChannel::server() { return thread_.get_server(); }
