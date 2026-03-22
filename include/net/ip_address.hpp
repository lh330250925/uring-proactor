#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <string>

class IpAddress
{
    sockaddr_in addr_;

public:
    IpAddress();
    IpAddress(const char *ip, uint16_t port);
    explicit IpAddress(uint16_t port);
    IpAddress(const sockaddr *addr, socklen_t len);

    const sockaddr *addr() const { return reinterpret_cast<const sockaddr *>(&addr_); }
    sockaddr *addr() { return reinterpret_cast<sockaddr *>(&addr_); }
    socklen_t len() const { return sizeof(sockaddr_in); }

    bool is_valid() const { return addr_.sin_family == AF_INET; }
    uint16_t port() const { return ntohs(addr_.sin_port); }
    std::string ip() const;
    std::string to_string() const;

    bool operator==(const IpAddress &o) const;
    bool operator!=(const IpAddress &o) const { return !(*this == o); }
};