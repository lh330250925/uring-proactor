// TCP + UDP echo server for testing.
// Build example (from project root):
//   g++ -std=c++23 -I include -o echo_server test/echo_server.cpp \
//       src/core/io_thread.cpp src/core/tcp_thread.cpp src/core/udp_thread.cpp \
//       src/core/awaiter.cpp src/core/io_ring.cpp src/core/buf_ring.cpp \
//       src/net/server.cpp src/net/channel.cpp src/net/handler.cpp \
//       src/net/acceptor.cpp src/net/tcp_channel_pool.cpp \
//       src/net/socket.cpp src/net/ip_address.cpp \
//       src/buffer/buffer.cpp src/buffer/buf_pool.cpp src/buffer/msghdr_pool.cpp \
//       src/util/log.cpp src/util/timestamp.cpp \
//       -luring -lnuma
//
// Test TCP echo:
//   nc localhost 8080
//
// Test UDP echo:
//   echo "hello" | nc -u localhost 8081

#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include "net/server.hpp"
#include "net/channel.hpp"
#include "util/log.hpp"

class TcpEchoServer : public TcpServer
{
public:
    TcpEchoServer(int port, int threads, bool cpu_affinity = false)
        : TcpServer(port, threads,
                    /*buf_pool_size=*/1024,
                    /*channel_capacity=*/64,
                    /*buf_ring_size=*/512,
                    /*pool_size=*/256,
                    /*queue_depth=*/256,
                    cpu_affinity)
    {}

    void on_accept(TcpChannel &) override {}

    void on_read(TcpChannel &ch) override
    {
        unsigned int avail = ch.read_buf_.readable_bytes();
        if (!avail) return;
        auto *res = ch.peek(avail);
        if (!res) return;
        for (unsigned i = 0; i < res->count; ++i)
            ch.append(res->data[i], res->size[i]);
        ch.consume(avail);
        ch.submit();
    }

    void on_close(TcpChannel &, int) override {}
};

class UdpEchoServer : public UdpServer
{
public:
    UdpEchoServer(int port, int threads, bool cpu_affinity = false)
        : UdpServer(port, threads,
                    /*buf_pool_size=*/1024,
                    /*buf_ring_size=*/512,
                    /*msghdr_pool_size=*/64,
                    /*channel_capacity=*/64,
                    /*queue_depth=*/256,
                    cpu_affinity)
    {}

    void on_read(UdpChannel &ch) override
    {
        while (true)
        {
            DgramMsg *msg = ch.read_buf_.peek();
            if (!msg)
                break;
            ch.append(msg->payload(), msg->payload_length());
            ch.submit(msg->peer_addr(), msg->peer_namelen());
            ch.consume();
        }
    }
};

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
static volatile bool g_running = true;

int main(int argc, char **argv)
{
    std::signal(SIGINT,  [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    bool cpu_affinity = false;
    Logger::LogLevel log_level = Logger::LogLevel::WARNING;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--sqpoll") == 0)
            cpu_affinity = true;
        else if (std::strcmp(argv[i], "--log-level") == 0 && i + 1 < argc)
        {
            ++i;
            if      (std::strcmp(argv[i], "debug")   == 0) log_level = Logger::LogLevel::DEBUG;
            else if (std::strcmp(argv[i], "info")    == 0) log_level = Logger::LogLevel::INFO;
            else if (std::strcmp(argv[i], "warning") == 0) log_level = Logger::LogLevel::WARNING;
            else if (std::strcmp(argv[i], "error")   == 0) log_level = Logger::LogLevel::ERROR;
        }
    }
    Logger::get_instance().set_log_level(log_level);

    int hw = static_cast<int>(std::thread::hardware_concurrency());
    // In SQPOLL mode each thread pair occupies 2 CPUs; leave 2 CPUs for bench client.
    int threads = cpu_affinity ? std::max(1, hw / 2 - 1) : hw;
    if (threads < 1) threads = 1;

    std::printf("Mode: %s, threads=%d\n",
                cpu_affinity ? "SQPOLL+cpu_affinity" : "normal", threads);

    TcpEchoServer tcp(8080, threads, cpu_affinity);
    UdpEchoServer udp(8081, threads, cpu_affinity);

    tcp.start();
    udp.start();

    std::printf("TCP echo listening on :8080\n");
    std::printf("UDP echo listening on :8081\n");
    std::printf("Press Ctrl+C to stop.\n");

    while (g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::printf("Shutting down.\n");
    return 0;
}
