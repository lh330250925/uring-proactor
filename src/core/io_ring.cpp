#include "core/io_ring.hpp"
#include <cassert>
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
void Sqe::prep_write_fixed(int fd, const void *buf, unsigned nbytes, off_t offset, int buf_index)
{
    io_uring_prep_write_fixed(sqe_, fd, buf, nbytes, offset, buf_index);
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
void Sqe::set_flags(unsigned char flags)
{
    sqe_->flags |= flags;
}
void Sqe::set_buf_group(unsigned short bgid)
{
    sqe_->buf_group = bgid;
}
void Sqe::prep_cancel_all()
{
    io_uring_prep_cancel64(sqe_, 0, IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_ANY);
}
Cqe::Cqe(io_uring_cqe *cqe) : cqe_(cqe) {}
int Cqe::get_res() const
{
    assert(cqe_ != nullptr);
    return cqe_->res;
}
void *Cqe::get_data() const
{
    assert(cqe_ != nullptr);
    return io_uring_cqe_get_data(cqe_);
}
unsigned int Cqe::get_flag() const
{
    assert(cqe_ != nullptr);
    return cqe_->flags;
}
IoRing::IoRing(unsigned queue_depth, unsigned flags)
    : queue_depth_(queue_depth)
{
    const int ret = io_uring_queue_init(queue_depth, &ring_, flags);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_queue_init failed");
}
IoRing::IoRing(unsigned queue_depth, io_uring_params &params)
    : queue_depth_(queue_depth)
{
    const int ret = io_uring_queue_init_params(queue_depth, &ring_, &params);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_queue_init_params failed");
}
IoRing::IoRing(unsigned queue_depth, io_uring_params *params)
    : queue_depth_(queue_depth)
{
    const int ret = params
                        ? io_uring_queue_init_params(queue_depth, &ring_, params)
                        : io_uring_queue_init(queue_depth, &ring_, 0);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_queue_init failed");
}
IoRing::~IoRing()
{
    io_uring_queue_exit(&ring_);
}
Sqe IoRing::get_sqe()
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr)
        throw std::runtime_error("io_uring_get_sqe returned null");
    return Sqe(sqe);
}
Cqe IoRing::wait_cqe()
{
    io_uring_cqe *cqe = nullptr;
    const int ret = io_uring_wait_cqe(&ring_, &cqe);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_wait_cqe failed");
    return Cqe(cqe);
}
Cqe IoRing::peek_cqe()
{
    io_uring_cqe *cqe = nullptr;
    const int ret = io_uring_peek_cqe(&ring_, &cqe);
    if (ret == -EAGAIN)
        return Cqe(nullptr);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_peek_cqe failed");
    return Cqe(cqe);
}
int IoRing::peek_batch_cqe(io_uring_cqe **cqes, unsigned count)
{
    return io_uring_peek_batch_cqe(&ring_, cqes, count);
}
int IoRing::submit()
{
    const int ret = io_uring_submit(&ring_);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_submit failed");
    return ret;
}
int IoRing::submit_and_wait(unsigned wait_nr)
{
    const int ret = io_uring_submit_and_wait(&ring_, wait_nr);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_submit_and_wait failed");
    return ret;
}
int IoRing::register_files(const int *fds, unsigned nr_fds)
{
    const int ret = io_uring_register_files(&ring_, fds, nr_fds);
    if (ret < 0)
        throw_uring_error(ret, "io_uring_register_files failed");
    return ret;
}
void IoRing::cqe_seen(const Cqe &cqe)
{
    io_uring_cqe_seen(&ring_, cqe.cqe_);
}
void IoRing::cqe_advance(unsigned nr)
{
    io_uring_cq_advance(&ring_, nr);
}
int IoRing::get_fd() const
{
    return ring_.ring_fd;
}
