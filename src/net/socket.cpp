#include "net/socket.hpp"
#include <cstdlib>

Socket::Socket(uint16_t port, Type type)
{
    int sock_type = (type == Type::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    fd_ = socket(AF_INET, SOCK_CLOEXEC | sock_type, 0);
    if (fd_ < 0)
    {
        log_error("Failed to create socket");
        std::exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        log_error("Failed to set SO_REUSEADDR");
        std::exit(EXIT_FAILURE);
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        log_error("Failed to set SO_REUSEPORT");
        std::exit(EXIT_FAILURE);
    }
    IpAddress ip_addr(port);
    if (bind(fd_, ip_addr.addr(), ip_addr.len()) < 0)
    {
        log_error("Failed to bind socket");
        std::exit(EXIT_FAILURE);
    }
}

Socket::~Socket()
{
    if (fd_ >= 0)
        close(fd_);
}

int Socket::listen(int backlog) const
{
    if (::listen(fd_, backlog) < 0)
    {
        log_error("Failed to listen on socket");
        return -1;
    }
    return 0;
}