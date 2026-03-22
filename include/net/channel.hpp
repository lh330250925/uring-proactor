#pragma once
#include "core/io_ring.hpp"
#include "core/awaiter.hpp"
#include "buffer/buffer.hpp"
#include "util/noncopyable.hpp"
#include "net/server.hpp"
#include "net/ip_address.hpp"
class Channel : NonCopyable
{
protected:
    constexpr static int CORO_FRAME_SIZE = 256;
    int fd_ = -1;
    IoRing &io_ring_;
    std::coroutine_handle<> read_handle_;
    std::coroutine_handle<> write_handle_;
    Channel(int fd, IoRing &io_ring) : fd_(fd), io_ring_(io_ring) {}
    ~Channel()
    {
        if (read_handle_)
            read_handle_.destroy();
        if (write_handle_)
            write_handle_.destroy();
    }

public:
    static constexpr int coro_frame_size() { return CORO_FRAME_SIZE; }
    char *read_coro_frame() { return read_coro_frame_; }
    char *write_coro_frame() { return write_coro_frame_; }
    IoRing &io_ring() { return io_ring_; }
    int fd() const { return fd_; }
    void reset(int fd) { fd_ = fd; }

private:
    char read_coro_frame_[CORO_FRAME_SIZE];
    char write_coro_frame_[CORO_FRAME_SIZE];
};

class TcpChannelPool;
class TcpThread;
class UdpThread;

class TcpChannel : public Channel
{
public:
    TcpChannel(int fd, TcpChannelPool &pool);
    NotifyToken notify_token_;
    ReadBufStream read_buf_;
    WriteBufStream write_buf_;
    bool submit()
    {
        if (!write_buf_.submit())
            return false;
        notify_token_.notify();
        return true;
    }
    bool append(const char *data, unsigned int size) { return write_buf_.append(data, size); }
    bool prepend(const char *data, unsigned int size) { return write_buf_.prepend(data, size); }
    ReadBufBase::PeekResult *peek(unsigned int size) { return read_buf_.peek(size); }
    bool consume(unsigned int size) { return read_buf_.consume(size); }
    void close();
    void assign(int fd);
    void in_flight_inc() { ++in_flight_; }
    void in_flight_dec()
    {
        if (--in_flight_ == 0)
            on_drained();
    }
    bool has_in_flight() const { return in_flight_ > 0; }
    void reset(int fd)
    {
        Channel::reset(fd);
        in_flight_ = 0;
    }
    bool is_closing() const { return notify_token_.is_closed(); }
    bool is_closed() const { return fd_ == -1; }
    TcpServer &server() { return server_; }
    TcpThread &thread();

private:
    void on_drained();
    TcpChannelPool &pool_;
    TcpServer &server_;
    int in_flight_ = 0;
};
class UdpChannel : public Channel
{
    UdpThread &thread_;
    void notify() { notify_token_.notify(); }

public:
    explicit UdpChannel(UdpThread &thread);
    void assign(int fd);
    bool consume() { return read_buf_.consume(); }
    bool submit(const IpAddress &addr)
    {
        if (!submit(addr.addr(), addr.len()))
            return false;
        notify();
        return true;
    }
    bool submit(const sockaddr *addr, socklen_t namelen)
    {
        MsghdrSlot *slot = write_buf_.acquire_slot();
        if (!slot)
            return false;
        std::memcpy(&slot->addr, addr, namelen);
        slot->hdr.msg_namelen = namelen;
        if (!write_buf_.submit(slot))
            return false;
        notify();
        return true;
    }
    bool append(const char *data, unsigned int size) { return write_buf_.append(data, size); }
    bool prepend(const char *data, unsigned int size) { return write_buf_.prepend(data, size); }
    UdpServer &server();
    UdpThread &thread() { return thread_; }
    NotifyToken notify_token_;
    ReadBufDgram read_buf_;
    WriteBufDgram write_buf_;
};