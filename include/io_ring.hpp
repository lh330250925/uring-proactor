#pragma once
#include <liburing.h>
#include "noncopyable.hpp"

class Sqe : NonCopyable
{
private:
    io_uring_sqe *const sqe_;

public:
    explicit Sqe(io_uring_sqe *sqe);
    ~Sqe();
    void prep_read(int fd, void *buf, unsigned nbytes, off_t offset);
    void prep_read_fixed(int fd, void *buf, unsigned nbytes, off_t offset, int buf_index);
    void prep_readv(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset);
    void prep_readv_fixed(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset, int buf_index);
    void prep_write(int fd, const void *buf, unsigned nbytes, off_t offset);
    void prep_write_fixed(int fd, const void *buf, unsigned nbytes, off_t offset, int buf_index);
    void prep_writev(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset);
    void prep_writev_fixed(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset, int buf_index);
    void prep_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
    void prep_multishot_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
    void prep_accept_direct(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags, int file_index);
    void prep_multishot_accept_direct(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
    void prep_recv(int fd, void *buf, unsigned nbytes, int flags);
    void prep_multishot_recv(int fd, void *buf, unsigned nbytes, int flags);
    void prep_recvmsg(int fd, struct msghdr *msg, int flags);
    void prep_multishot_recvmsg(int fd, struct msghdr *msg, int flags);
    void prep_send(int fd, const void *buf, unsigned nbytes, int flags);
    void prep_sendmsg(int fd, struct msghdr *msg, int flags);
    void prep_msg_ring(int fd, unsigned len, void *data, int flags);
    void prep_msg_ring_fd(int fd, int source_fd, int target_fd, void *data, int flags);
    void prep_msg_ring_fd_alloc(int fd, unsigned len, void *data, int flags);
    void set_data(void *data);
};

class Cqe : NonCopyable
{
private:
    io_uring_cqe *const cqe_;
    friend class IoRing;

public:
    Cqe(io_uring_cqe *cqe);
    ~Cqe();
    int get_res() const;
    void *get_data() const;
    int get_flag() const;
    bool empty() const
    {
        return cqe_ == nullptr;
    }
};

class IoRing : NonCopyable
{
private:
    io_uring ring;
    static const int QUEUE_DEPTH = 256;

public:
    IoRing(const unsigned &flags = 0);
    ~IoRing();
    Sqe get_sqe();
    Cqe wait_cqe();
    Cqe peek_cqe();
    int submit();
    int submit_and_wait(unsigned wait_nr);
    void cqe_seen(const Cqe &cqe);
    void cqe_advance(unsigned nr);
    int register_files(const int *fds, unsigned nr_fds);
    int get_fd() const;
    io_uring& get_ring() { return ring; }
};
