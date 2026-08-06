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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include "net/server.hpp"
#include "net/channel.hpp"
#include "util/log.hpp"

struct ProtocolConfig
{
    int buf_pool_size = 1024;
    int channel_capacity = 64;
    int buf_ring_size = 512;
    unsigned queue_depth = 512;
};

struct ServerConfig
{
    ProtocolConfig tcp{2048, 16, 1024, 512};
    ProtocolConfig udp{};
    int tcp_pool_size = 2048;
    int udp_msghdr_pool_size = 256;
};

static bool is_power_of_two(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

class TcpEchoServer : public TcpServer
{
public:
    TcpEchoServer(int port, int threads, const ServerConfig &config,
                  bool cpu_affinity = false)
        : TcpServer(port, threads,
                    config.tcp.buf_pool_size,
                    config.tcp.channel_capacity,
                    config.tcp.buf_ring_size,
                    config.tcp_pool_size,
                    config.tcp.queue_depth,
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
    UdpEchoServer(int port, int threads, const ServerConfig &config,
                  bool cpu_affinity = false)
        : UdpServer(port, threads,
                    config.udp.buf_pool_size,
                    config.udp.buf_ring_size,
                    config.udp_msghdr_pool_size,
                    config.udp.channel_capacity,
                    config.udp.queue_depth,
                    cpu_affinity)
    {}

    void on_read(UdpChannel &ch) override
    {
        while (true)
        {
            DgramMsg *msg = ch.read_buf_.peek();
            if (!msg)
                break;
            if (ch.append(msg->payload(), msg->payload_length()))
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
    bool run_tcp = true;
    bool run_udp = true;
    int requested_threads = 0;
    ServerConfig config;
    Logger::LogLevel log_level = Logger::LogLevel::WARNING;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--sqpoll") == 0)
            cpu_affinity = true;
        else if (std::strcmp(argv[i], "--no-tcp") == 0)
            run_tcp = false;
        else if (std::strcmp(argv[i], "--no-udp") == 0)
            run_udp = false;
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            requested_threads = std::max(1, std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--pool-size") == 0 && i + 1 < argc)
            config.tcp_pool_size = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--msghdr-pool-size") == 0 && i + 1 < argc)
            config.udp_msghdr_pool_size = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--channel-capacity") == 0 && i + 1 < argc)
            config.tcp.channel_capacity = config.udp.channel_capacity = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--buf-pool-size") == 0 && i + 1 < argc)
            config.tcp.buf_pool_size = config.udp.buf_pool_size = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--buf-ring-size") == 0 && i + 1 < argc)
            config.tcp.buf_ring_size = config.udp.buf_ring_size = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--queue-depth") == 0 && i + 1 < argc)
            config.tcp.queue_depth = config.udp.queue_depth = static_cast<unsigned>(std::atoi(argv[++i]));
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

    if (!is_power_of_two(config.tcp.buf_pool_size) ||
        !is_power_of_two(config.tcp.channel_capacity) ||
        !is_power_of_two(config.tcp.buf_ring_size) ||
        !is_power_of_two(static_cast<int>(config.tcp.queue_depth)) ||
        !is_power_of_two(config.udp.buf_pool_size) ||
        !is_power_of_two(config.udp.channel_capacity) ||
        !is_power_of_two(config.udp.buf_ring_size) ||
        !is_power_of_two(static_cast<int>(config.udp.queue_depth)) ||
        !is_power_of_two(config.tcp_pool_size) ||
        !is_power_of_two(config.udp_msghdr_pool_size))
    {
        std::fprintf(stderr, "Buffer, pool, capacity, and queue values must be positive powers of two.\n");
        return 2;
    }

    int hw = static_cast<int>(std::thread::hardware_concurrency());
    // In SQPOLL mode each thread pair occupies 2 CPUs; leave 2 CPUs for bench client.
    int threads = requested_threads > 0
                ? requested_threads
                : (cpu_affinity ? std::max(1, hw / 2 - 1) : hw);
    if (threads < 1) threads = 1;

    std::printf("Mode: %s, threads=%d\n",
                cpu_affinity ? "SQPOLL+cpu_affinity" : "normal", threads);
    std::printf("TCP config: pool=%d, read_capacity=%d, write_capacity=%d, buf_pool=%d, buf_ring=%d, queue=%u\n",
                config.tcp_pool_size, config.tcp.buf_ring_size, config.tcp.channel_capacity,
                config.tcp.buf_pool_size, config.tcp.buf_ring_size, config.tcp.queue_depth);
    std::printf("UDP config: msghdr_pool=%d, read_capacity=%d, write_capacity=%d, buf_pool=%d, buf_ring=%d, queue=%u\n",
                config.udp_msghdr_pool_size, config.udp.buf_ring_size,
                config.udp.channel_capacity, config.udp.buf_pool_size,
                config.udp.buf_ring_size, config.udp.queue_depth);

    if (!run_tcp && !run_udp)
    {
        std::fprintf(stderr, "At least one protocol must be enabled.\n");
        return 2;
    }

    std::unique_ptr<TcpEchoServer> tcp;
    std::unique_ptr<UdpEchoServer> udp;
    if (run_tcp)
    {
        tcp = std::make_unique<TcpEchoServer>(8080, threads, config, cpu_affinity);
        tcp->start();
        std::printf("TCP echo listening on :8080\n");
    }
    if (run_udp)
    {
        udp = std::make_unique<UdpEchoServer>(8081, threads, config, cpu_affinity);
        udp->start();
        std::printf("UDP echo listening on :8081\n");
    }

    std::printf("Press Ctrl+C to stop.\n");

    while (g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::printf("Shutting down.\n");
    return 0;
}
