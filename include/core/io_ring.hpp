#pragma once
#include <liburing.h>
#include "util/noncopyable.hpp"

class Sqe : NonCopyable
{
private:
    io_uring_sqe *const sqe_;

public:
    explicit Sqe(io_uring_sqe *sqe);
    ~Sqe() = default;
    void prep_read(int fd, void *buf, unsigned nbytes, off_t offset);
    void prep_read_fixed(int fd, void *buf, unsigned nbytes, off_t offset, int buf_index);
    void prep_readv(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset);
    void prep_write(int fd, const void *buf, unsigned nbytes, off_t offset);
    void prep_write_fixed(int fd, const void *buf, unsigned nbytes, off_t offset, int buf_index);
    void prep_writev(int fd, const struct iovec *iov, unsigned nr_vecs, off_t offset);
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
    void prep_cancel_all();
    void set_data(void *data);
    void set_flags(unsigned char flags);
    void set_buf_group(unsigned short bgid);
};

class Cqe : NonCopyable
{
private:
    io_uring_cqe *const cqe_;
    friend class IoRing;

public:
    explicit Cqe(io_uring_cqe *cqe);
    ~Cqe() = default;
    int get_res() const;
    void *get_data() const;
    unsigned int get_flag() const;
    bool empty() const { return cqe_ == nullptr; }
};

class IoRing : NonCopyable
{
public:
private:
    io_uring ring_;
    unsigned queue_depth_;

public:
    explicit IoRing(unsigned queue_depth, unsigned flags = 0);
    explicit IoRing(unsigned queue_depth, io_uring_params &params);
    explicit IoRing(unsigned queue_depth, io_uring_params *params);
    unsigned get_queue_depth() const { return queue_depth_; }
    ~IoRing();
    Sqe get_sqe();
    Cqe wait_cqe();
    Cqe peek_cqe();
    int peek_batch_cqe(io_uring_cqe **cqes, unsigned count);
    int submit();
    int submit_and_wait(unsigned wait_nr);
    void cqe_seen(const Cqe &cqe);
    void cqe_advance(unsigned nr);
    int register_files(const int *fds, unsigned nr_fds);
    int get_fd() const;
    io_uring &get_ring() { return ring_; }
    const io_uring &get_ring() const { return ring_; }
};
