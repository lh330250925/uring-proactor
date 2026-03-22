#include "net/acceptor.hpp"
#include <format>
#include <unistd.h>
#include "core/tcp_thread.hpp"
#include "net/socket.hpp"
#include "util/log.hpp"

Acceptor::Acceptor(std::coroutine_handle<promise_type> h) : handle_(h) {}

Acceptor::~Acceptor()
{
    if (handle_)
        handle_.destroy();
}

void Acceptor::resume()
{
    if (handle_ && !handle_.done())
        handle_.resume();
}

Acceptor accept_loop(TcpThread &thread)
{
    auto &p = co_await GetPromise<Acceptor::promise_type>{};
    Socket listen_socket(thread.get_server().get_port(), Socket::Type::TCP);
    listen_socket.listen();
    auto sqe = thread.get_ring().get_sqe();
    sqe.prep_multishot_accept(listen_socket.fd(), nullptr, nullptr, SOCK_CLOEXEC);
    sqe.set_data(&p.token_);
    while (true)
    {
        auto fd = co_await p.token_;
        if (fd < 0)
        {
            log_error(std::format("accept error: {}", -fd));
            break;
        }
        if (!(p.token_.flag() & IORING_CQE_F_MORE))
        {
            auto sqe = thread.get_ring().get_sqe();
            sqe.prep_multishot_accept(listen_socket.fd(), nullptr, nullptr, SOCK_CLOEXEC);
            sqe.set_data(&p.token_);
        }
        TcpChannel *ch = thread.get_tcp_channel_pool().acquire(fd);
        if (!ch)
        {
            LOG_ERROR("channel pool exhausted, dropping connection");
            ::close(fd);
            continue;
        }
    }
}
