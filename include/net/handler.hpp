#pragma once
#include <coroutine>
#include <cassert>
#include "util/noncopyable.hpp"
#include "core/awaiter.hpp"
#include "net/channel.hpp"
#include "net/socket.hpp"

template <typename ChannelT, char *(Channel::*Frame)(), typename TokenT = Token>
struct ChannelHandler : NonCopyable
{
    struct promise_type
    {
        ChannelT *channel_;
        TokenT token_;
        explicit promise_type(ChannelT &ch) : channel_(&ch) {}
        static void *operator new(size_t size, ChannelT &ch)
        {
            assert(size <= (size_t)ch.coro_frame_size());
            return (ch.*Frame)();
        }
        void operator delete(void *, size_t) noexcept {}
        ChannelHandler get_return_object() noexcept
        {
            return ChannelHandler{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
    };
    std::coroutine_handle<promise_type> handle_;
    explicit ChannelHandler(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ChannelHandler(ChannelHandler &&o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    ChannelHandler &operator=(ChannelHandler &&o) noexcept
    {
        if (handle_ && handle_.done())
            handle_.destroy();
        handle_ = std::exchange(o.handle_, {});
        return *this;
    }
    std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(handle_, {});
    }
};

using TcpReadHandler = ChannelHandler<TcpChannel, &Channel::read_coro_frame>;
using TcpWriteHandler = ChannelHandler<TcpChannel, &Channel::write_coro_frame>;
using UdpReadHandler = ChannelHandler<UdpChannel, &Channel::read_coro_frame>;
using UdpWriteHandler = ChannelHandler<UdpChannel, &Channel::write_coro_frame>;

TcpReadHandler tcp_read_loop(TcpChannel &ch);
TcpWriteHandler tcp_write_loop(TcpChannel &ch);
UdpReadHandler udp_read_loop(UdpChannel &ch);
UdpWriteHandler udp_write_loop(UdpChannel &ch);