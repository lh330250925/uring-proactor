#pragma once
#include <coroutine>
#include <exception>
#include "util/noncopyable.hpp"
#include "core/awaiter.hpp"

class TcpThread;

struct Acceptor : NonCopyable
{
    struct promise_type
    {
        Token token_;
        Acceptor get_return_object() noexcept
        {
            return Acceptor{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    std::coroutine_handle<promise_type> handle_;
    explicit Acceptor(std::coroutine_handle<promise_type> h);
    ~Acceptor();
    void resume();
    std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(handle_, {});
    }
};

Acceptor accept_loop(TcpThread &thread);
