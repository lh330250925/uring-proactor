#pragma once
#include <coroutine>
#include <utility>

template <typename Promise>
struct GetPromise
{
    std::coroutine_handle<Promise> handle_;

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        handle_ = h;
        return false;
    }
    Promise &await_resume() noexcept { return handle_.promise(); }
};

struct Token
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle_ = h; }
    int await_resume() noexcept { return res_; }
    virtual void complete(int res, int flag) noexcept;
    int res() const noexcept { return res_; }
    int flag() const noexcept { return flag_; }

private:
    std::coroutine_handle<> handle_;
    int res_ = 0;
    int flag_ = 0;
};

struct NotifyToken
{
    bool await_ready() noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept { handle_ = h; }
    void await_resume() noexcept {}
    void notify() noexcept;
    void notify_close() noexcept;
    bool is_closed() const noexcept { return closed_; }

private:
    bool pending_ = false;
    bool closed_ = false;
    std::coroutine_handle<> handle_;
};
class UdpChannel;
struct MsghdrSlot;

struct MsgToken : public Token
{
    UdpChannel *channel_ = nullptr;
    void complete(int res, int flag) noexcept override;
};