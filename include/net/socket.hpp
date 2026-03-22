#pragma once
#include <sys/socket.h>
#include <unistd.h>
#include "util/log.hpp"
#include "net/ip_address.hpp"

class Socket
{
    int fd_;

public:
    enum class Type
    {
        TCP,
        UDP
    };

    Socket(uint16_t port, Type type = Type::TCP);
    ~Socket();
    int fd() const { return fd_; }
    int get_fd() const { return fd_; }
    int listen(int backlog = 4096) const;
};