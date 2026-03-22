#pragma once
#include <cassert>
#include <liburing.h>

class DgramMsg
{
    char* buf_;
    unsigned int size_;
    io_uring_recvmsg_out* out_;
    msghdr* msghdr_;
    public:
    DgramMsg(char* buf, unsigned int size, msghdr* msghdr) : buf_(buf), size_(size), msghdr_(msghdr)
    {
        out_ = io_uring_recvmsg_validate(buf, size, msghdr);
        assert(out_ != nullptr);
    }
    ~DgramMsg() = default;
    char* buf() const { return buf_; }
    unsigned int size() const { return size_; }
    io_uring_recvmsg_out* out() const { return out_; }
    const sockaddr *peer_addr() const { return static_cast<const sockaddr *>(io_uring_recvmsg_name(out_)); }
    socklen_t peer_namelen() const { return out_->namelen; }
    const char* payload() const { return static_cast<const char *>(io_uring_recvmsg_payload(out_, msghdr_)); }
    unsigned int payload_length() const { return io_uring_recvmsg_payload_length(out_, size_, msghdr_); }
};