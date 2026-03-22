#include "net/ip_address.hpp"
#include <cassert>
#include <cstring>

IpAddress::IpAddress()
{
    std::memset(&addr_, 0, sizeof(addr_));
}

IpAddress::IpAddress(const char *ip, uint16_t port)
{
    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr_.sin_addr);
}

IpAddress::IpAddress(uint16_t port)
{
    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    addr_.sin_addr.s_addr = htonl(INADDR_ANY);
}

IpAddress::IpAddress(const sockaddr *addr, socklen_t len)
{
    assert(len >= sizeof(sockaddr_in));
    std::memcpy(&addr_, addr, sizeof(sockaddr_in));
}

std::string IpAddress::ip() const
{
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    return buf;
}

std::string IpAddress::to_string() const
{
    return ip() + ":" + std::to_string(port());
}

bool IpAddress::operator==(const IpAddress &o) const
{
    return addr_.sin_family == o.addr_.sin_family &&
           addr_.sin_port == o.addr_.sin_port &&
           addr_.sin_addr.s_addr == o.addr_.sin_addr.s_addr;
}
