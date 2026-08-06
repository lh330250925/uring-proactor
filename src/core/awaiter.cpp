#include "core/awaiter.hpp"
#include "net/channel.hpp"
#include "buffer/buffer.hpp"

void Token::complete(int res, int flag) noexcept
{
    res_  = res;
    flag_ = flag;
    if (auto h = std::exchange(handle_, {}); h)
        h.resume();
}

bool NotifyToken::await_ready() noexcept
{
    if (pending_) { pending_ = false; return true; }
    return false;
}

void NotifyToken::notify() noexcept
{
    if (auto h = std::exchange(handle_, {}); h)
        h.resume();
    else
        pending_ = true;
}

void MsgToken::complete(int res, int) noexcept
{
    auto *slot = reinterpret_cast<MsghdrSlot *>(this);
    channel_->write_buf_.release_slot(slot);
    channel_->server().on_write(*channel_, res);
}

void NotifyToken::notify_close() noexcept
{
    closed_ = true;
    if (auto h = std::exchange(handle_, {}); h)
        h.resume();
    else
        pending_ = true;
}
