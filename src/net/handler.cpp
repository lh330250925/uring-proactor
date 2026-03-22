#include "net/handler.hpp"
#include "core/tcp_thread.hpp"
#include "core/udp_thread.hpp"

TcpReadHandler tcp_read_loop(TcpChannel &ch)
{
    auto &p = co_await GetPromise<TcpReadHandler::promise_type>{};
    auto sqe = ch.io_ring().get_sqe();
    sqe.prep_multishot_recv(ch.fd(), NULL, 0, 0);
    sqe.set_flags(IOSQE_BUFFER_SELECT);
    sqe.set_buf_group(ch.read_buf_.buf_group());
    sqe.set_data(&p.token_);
    p.channel_->in_flight_inc();
    while (true)
    {
        auto res = co_await p.token_;
        p.channel_->in_flight_dec();
        if (res <= 0)
        {
            p.channel_->server().on_close(*p.channel_, res);
            if (!p.channel_->is_closing())
                p.channel_->close();
            break;
        }
        if (!(p.token_.flag() & IORING_CQE_F_MORE))
        {
            auto sqe = ch.io_ring().get_sqe();
            sqe.prep_multishot_recv(ch.fd(), NULL, 0, 0);
            sqe.set_flags(IOSQE_BUFFER_SELECT);
            sqe.set_buf_group(ch.read_buf_.buf_group());
            sqe.set_data(&p.token_);
            p.channel_->in_flight_inc();
        }
        auto idx = (unsigned)(p.token_.flag() >> IORING_CQE_BUFFER_SHIFT);
        p.channel_->read_buf_.set_buf_data_size(idx, (unsigned)res);
        p.channel_->read_buf_.push_buf(idx);
        p.channel_->thread().mark_pending_read(p.channel_);
    }
    p.channel_->read_buf_.reset();
}

TcpWriteHandler tcp_write_loop([[maybe_unused]] TcpChannel &ch)
{
    auto &p = co_await GetPromise<TcpWriteHandler::promise_type>{};
    while (true)
    {
        co_await p.channel_->notify_token_;
        if (p.channel_->is_closing() && !p.channel_->has_in_flight())
            break;
        int count = 0;
        const iovec *vecs = p.channel_->write_buf_.peek_iovec(count);
        if (count == 0)
            continue;
        p.channel_->write_buf_.set_release_guard(count);
        auto sqe = p.channel_->io_ring().get_sqe();
        sqe.prep_writev(p.channel_->fd(), vecs, (unsigned)count, 0);
        sqe.set_data(&p.token_);
        p.channel_->in_flight_inc();
        auto res = co_await p.token_;
        p.channel_->in_flight_dec();
        if (res <= 0)
        {
            if (!p.channel_->is_closing())
                p.channel_->close();
            break;
        }
        p.channel_->write_buf_.release();
        p.channel_->server().on_write(*p.channel_, res);
    }
    p.channel_->write_buf_.reset();
}

UdpReadHandler udp_read_loop(UdpChannel &ch)
{
    auto &p = co_await GetPromise<UdpReadHandler::promise_type>{};
    auto sqe = ch.io_ring().get_sqe();
    sqe.prep_multishot_recvmsg(ch.fd(), p.channel_->read_buf_.recv_msghdr(), 0);
    sqe.set_flags(IOSQE_BUFFER_SELECT);
    sqe.set_buf_group(p.channel_->read_buf_.buf_group());
    sqe.set_data(&p.token_);
    while (true)
    {
        auto res = co_await p.token_;
        if (!(p.token_.flag() & IORING_CQE_F_MORE))
        {
            auto sqe = ch.io_ring().get_sqe();
            sqe.prep_multishot_recvmsg(ch.fd(), p.channel_->read_buf_.recv_msghdr(), 0);
            sqe.set_flags(IOSQE_BUFFER_SELECT);
            sqe.set_buf_group(p.channel_->read_buf_.buf_group());
            sqe.set_data(&p.token_);
        }
        if (res < 0)
            continue;
        unsigned idx = p.token_.flag() >> IORING_CQE_BUFFER_SHIFT;
        p.channel_->read_buf_.set_buf_data_size(idx, (unsigned)res);
        p.channel_->read_buf_.push_buf(idx);
        p.channel_->thread().get_server().on_read(*p.channel_);
    }
}
UdpWriteHandler udp_write_loop(UdpChannel &ch)
{
    auto &p = co_await GetPromise<UdpWriteHandler::promise_type>{};
    while (true)
    {
        co_await p.channel_->notify_token_;
        int count = ch.write_buf_.slot_queue_size();
        for (int i = 0; i < count; ++i)
        {
            auto *slot = ch.write_buf_.peek_slot(i);
            slot->token_.channel_ = &ch;
            auto sqe = ch.io_ring().get_sqe();
            sqe.prep_sendmsg(ch.fd(), &slot->hdr, 0);
            sqe.set_data(&slot->token_);
        }
        ch.write_buf_.clear_slot_queue();
    }
}