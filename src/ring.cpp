#include "ring.hpp"
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace
{
    void throw_uring_error(int ret, const char *what)
    {
        throw std::system_error(-ret, std::generic_category(), what);
    }
}

Sqe::Sqe(io_uring_sqe *sqe) : sqe_(sqe) {}

Sqe::~Sqe() {}

void Sqe::prep_read(int fd, void *buf, unsigned nbytes, off_t offset)
{
    io_uring_prep_read(sqe_, fd, buf, nbytes, offset);
}
void Sqe::prep_read_fixed(int fd, void *buf, unsigned nbytes, off_t offset, int buf_index)
{
    io_uring_prep_read_fixed(sqe_, fd, buf, nbytes, offset, buf_index);
}
void Sqe::prep_readv(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset)
{
    io_uring_prep_readv(sqe_, fd, iov, nr_vecs, offset);
}
void Sqe::prep_write(int fd, const void *buf, unsigned nbytes, off_t offset)
{
    io_uring_prep_write(sqe_, fd, buf, nbytes, offset);
}
void Sqe::prep_writev(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset)
{
    io_uring_prep_writev(sqe_, fd, iov, nr_vecs, offset);
}
void Sqe::prep_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    io_uring_prep_accept(sqe_, fd, addr, addrlen, flags);
}
void Sqe::prep_multishot_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    io_uring_prep_multishot_accept(sqe_, fd, addr, addrlen, flags);
}
void Sqe::prep_accept_direct(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags, int file_index)
{
    io_uring_prep_accept_direct(sqe_, fd, addr, addrlen, flags, file_index);
}
void Sqe::prep_multishot_accept_direct(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    io_uring_prep_multishot_accept_direct(sqe_, fd, addr, addrlen, flags);
}
void Sqe::prep_recv(int fd, void *buf, unsigned nbytes, int flags)
{
    io_uring_prep_recv(sqe_, fd, buf, nbytes, flags);
}
void Sqe::prep_multishot_recv(int fd, void *buf, unsigned nbytes, int flags)
{
    io_uring_prep_recv_multishot(sqe_, fd, buf, nbytes, flags);
}
void Sqe::prep_recvmsg(int fd, struct msghdr *msg, int flags)
{
    io_uring_prep_recvmsg(sqe_, fd, msg, flags);
}
void Sqe::prep_multishot_recvmsg(int fd, struct msghdr *msg, int flags)
{
    io_uring_prep_recvmsg_multishot(sqe_, fd, msg, flags);
}
void Sqe::prep_send(int fd, const void *buf, unsigned nbytes, int flags)
{
    io_uring_prep_send(sqe_, fd, buf, nbytes, flags);
}
void Sqe::prep_sendmsg(int fd, struct msghdr *msg, int flags)
{
    io_uring_prep_sendmsg(sqe_, fd, msg, flags);
}
void Sqe::prep_msg_ring(int fd, unsigned len, void *data, int flags)
{
    io_uring_prep_msg_ring(sqe_, fd, len, (__u64)data, flags);
}
void Sqe::prep_msg_ring_fd(int fd, int source_fd, int target_fd, void *data, int flags)
{
    io_uring_prep_msg_ring_fd(sqe_, fd, source_fd, target_fd, (__u64)data, flags);
}
void Sqe::prep_msg_ring_fd_alloc(int fd, unsigned len, void *data, int flags)
{
    io_uring_prep_msg_ring_fd_alloc(sqe_, fd, len, (__u64)data, flags);
}
void Sqe::set_data(void *data)
{
    io_uring_sqe_set_data(sqe_, data);
}
Cqe::Cqe(io_uring_cqe *cqe) : cqe_(cqe) {}
Cqe::~Cqe() {}
int Cqe::get_res() const
{
    return cqe_->res;
}
void *Cqe::get_data() const
{
    return io_uring_cqe_get_data(cqe_);
}
int Cqe::get_flag() const
{
    return cqe_->flags;
}
Ring::Ring(const unsigned &flags)
{
    const int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, flags);
    if (ret < 0)
    {
        throw_uring_error(ret, "io_uring_queue_init failed");
    }
}
Ring::~Ring()
{
    io_uring_queue_exit(&ring);
}
Sqe Ring::get_sqe()
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (sqe == nullptr)
    {
        throw std::runtime_error("io_uring_get_sqe returned null");
    }
    return Sqe(sqe);
}
Cqe Ring::wait_cqe()
{
    io_uring_cqe *cqe = nullptr;
    const int ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0 || cqe == nullptr)
    {
        if (ret < 0)
        {
            throw_uring_error(ret, "io_uring_wait_cqe failed");
        }
        throw std::runtime_error("io_uring_wait_cqe returned null");
    }
    return {cqe};
}
Cqe Ring::peek_cqe()
{
    io_uring_cqe *cqe = nullptr;
    const int ret = io_uring_peek_cqe(&ring, &cqe);
    if (ret < 0)
    {
        throw_uring_error(ret, "io_uring_peek_cqe failed");
    }
    if (cqe == nullptr)
    {
        throw std::runtime_error("io_uring_peek_cqe returned null");
    }
    return {cqe};
}
int Ring::submit()
{
    const int ret = io_uring_submit(&ring);
    if (ret < 0)
    {
        throw_uring_error(ret, "io_uring_submit failed");
    }
    return ret;
}
int Ring::submit_and_wait(unsigned wait_nr)
{
    const int ret = io_uring_submit_and_wait(&ring, wait_nr);
    if (ret < 0)
    {
        throw_uring_error(ret, "io_uring_submit_and_wait failed");
    }
    return ret;
}
int Ring::register_files(int *fds, unsigned nr_fds)
{
    const int ret = io_uring_register_files(&ring, fds, nr_fds);
    if (ret < 0)
    {
        throw_uring_error(ret, "io_uring_register_files failed");
    }
    return ret;
}
void Ring::cqe_seen(const Cqe &cqe)
{
    io_uring_cqe_seen(&ring, cqe.cqe_);
}
void Ring::cqe_advance(unsigned nr)
{
    io_uring_cq_advance(&ring, nr);
}
int Ring::get_fd() const
{
    return ring.ring_fd;
}