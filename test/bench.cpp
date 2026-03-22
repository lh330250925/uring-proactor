// Proactor-server performance benchmark
// Tests TCP echo latency/throughput and UDP echo PPS/latency.
//
// Build (from project root, optimized):
//   g++ -std=c++23 -O2 -o /tmp/bench test/bench.cpp -lpthread
//
// Usage:
//   /tmp/bench [options]
//
// Options:
//   --addr ADDR       server address          (default: 127.0.0.1)
//   --tcp-port PORT   TCP port                (default: 8080)
//   --udp-port PORT   UDP port                (default: 8081)
//   --conns N         concurrent TCP conns    (default: 100)
//   --tcp-size N      TCP message size bytes  (default: 64)
//   --udp-size N      UDP message size bytes  (default: 64, min 16)
//   --duration S      test duration seconds   (default: 5)
//   --udp-rate N      UDP send rate pps       (default: 200000, 0=unlimited)
//   --no-tcp          skip TCP test
//   --no-udp          skip UDP test

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using ns_t  = std::chrono::nanoseconds;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static double percentile(std::vector<long> &v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p / 100.0 * static_cast<double>(v.size()));
    if (idx >= v.size()) idx = v.size() - 1;
    return static_cast<double>(v[idx]) / 1000.0; // ns → µs
}

static double avg_us(const std::vector<long> &v)
{
    if (v.empty()) return 0.0;
    long long sum = 0;
    for (long x : v) sum += x;
    return static_cast<double>(sum) / static_cast<double>(v.size()) / 1000.0;
}

static int make_tcp_socket(const char *addr, int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int yes = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, addr, &sa.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof sa) < 0) {
        perror("tcp connect");
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int r = static_cast<int>(::send(fd, buf + sent, static_cast<size_t>(len - sent), 0));
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

static bool recv_all(int fd, char *buf, int len)
{
    int got = 0;
    while (got < len) {
        int r = static_cast<int>(::recv(fd, buf + got, static_cast<size_t>(len - got), 0));
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP benchmark (ping-pong per connection)
// ─────────────────────────────────────────────────────────────────────────────

struct TcpWorkerResult {
    long             ops   = 0;
    long             bytes = 0;
    std::vector<long> latency_ns;
};

static void tcp_worker(const char *addr, int port, int msg_size,
                       TcpWorkerResult &out,
                       std::atomic<bool> &go, std::atomic<bool> &stop)
{
    int fd = make_tcp_socket(addr, port);
    if (fd < 0) return;

    std::vector<char> sendbuf(msg_size, 'T');
    std::vector<char> recvbuf(msg_size);
    out.latency_ns.reserve(500'000);

    // Spin until start signal
    while (!go.load(std::memory_order_acquire))
        ;

    while (!stop.load(std::memory_order_relaxed)) {
        auto t0 = Clock::now();
        if (!send_all(fd, sendbuf.data(), msg_size)) break;
        if (!recv_all(fd, recvbuf.data(), msg_size)) break;
        auto t1 = Clock::now();

        long elapsed = std::chrono::duration_cast<ns_t>(t1 - t0).count();
        out.latency_ns.push_back(elapsed);
        ++out.ops;
        out.bytes += msg_size * 2; // sent + received
    }
    ::close(fd);
}

static void run_tcp_bench(const char *addr, int tcp_port,
                          int conns, int msg_size, int duration_s)
{
    printf("\n┌── TCP echo benchmark ──────────────────────────────────────\n");
    printf("│  addr=%-15s port=%-6d connections=%-4d msg_size=%d bytes\n",
           addr, tcp_port, conns, msg_size);
    printf("│  duration=%ds\n", duration_s);

    std::vector<TcpWorkerResult> results(conns);
    std::vector<std::thread>     threads;
    threads.reserve(conns);
    std::atomic<bool> go{false}, stop{false};

    for (int i = 0; i < conns; ++i)
        threads.emplace_back(tcp_worker, addr, tcp_port, msg_size,
                             std::ref(results[i]), std::ref(go), std::ref(stop));

    // Wait for all connections to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::seconds(duration_s));
    stop.store(true, std::memory_order_relaxed);

    for (auto &t : threads) t.join();

    // Aggregate
    long             total_ops   = 0;
    long             total_bytes = 0;
    std::vector<long> all_lat;
    for (auto &r : results) {
        total_ops   += r.ops;
        total_bytes += r.bytes;
        all_lat.insert(all_lat.end(), r.latency_ns.begin(), r.latency_ns.end());
    }

    double qps  = static_cast<double>(total_ops)   / duration_s;
    double mbps = static_cast<double>(total_bytes)  / duration_s / 1e6;

    printf("│\n");
    printf("│  Throughput:  %10.0f msg/s    %8.2f MB/s\n", qps, mbps);
    if (!all_lat.empty()) {
        printf("│  RTT latency:\n");
        printf("│    avg  = %8.1f µs\n", avg_us(all_lat));
        printf("│    p50  = %8.1f µs\n", percentile(all_lat, 50.0));
        printf("│    p90  = %8.1f µs\n", percentile(all_lat, 90.0));
        printf("│    p99  = %8.1f µs\n", percentile(all_lat, 99.0));
        printf("│    p999 = %8.1f µs\n", percentile(all_lat, 99.9));
        printf("│    max  = %8.1f µs\n",
               static_cast<double>(all_lat.back()) / 1000.0); // already sorted
    }
    printf("└────────────────────────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-connection scaling sweep (1, 10, 50, 100 connections)
// ─────────────────────────────────────────────────────────────────────────────

static void run_tcp_scaling(const char *addr, int tcp_port,
                            int msg_size, int duration_s)
{
    printf("\n┌── TCP scaling sweep ─────────────────────── msg_size=%d bytes ──\n",
           msg_size);
    printf("│  %-8s  %14s  %10s  %10s  %10s  %10s  %10s\n",
           "conns", "QPS", "MB/s", "p50(µs)", "p90(µs)", "p99(µs)", "p999(µs)");
    printf("│  %-8s  %14s  %10s  %10s  %10s  %10s  %10s\n",
           "─────", "─────────────", "─────────", "─────────", "─────────", "─────────", "─────────");

    for (int conns : {1, 10, 50, 100}) {
        std::vector<TcpWorkerResult> results(conns);
        std::vector<std::thread>     threads;
        threads.reserve(conns);
        std::atomic<bool> go{false}, stop{false};

        for (int i = 0; i < conns; ++i)
            threads.emplace_back(tcp_worker, addr, tcp_port, msg_size,
                                 std::ref(results[i]), std::ref(go), std::ref(stop));

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        go.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::seconds(duration_s));
        stop.store(true, std::memory_order_relaxed);
        for (auto &t : threads) t.join();

        long total_ops = 0, total_bytes = 0;
        std::vector<long> all_lat;
        for (auto &r : results) {
            total_ops   += r.ops;
            total_bytes += r.bytes;
            all_lat.insert(all_lat.end(), r.latency_ns.begin(), r.latency_ns.end());
        }

        double qps  = static_cast<double>(total_ops)  / duration_s;
        double mbps = static_cast<double>(total_bytes) / duration_s / 1e6;
        printf("│  %-8d  %14.0f  %10.2f  %10.1f  %10.1f  %10.1f  %10.1f\n",
               conns, qps, mbps,
               percentile(all_lat, 50.0),
               percentile(all_lat, 90.0),
               percentile(all_lat, 99.0),
               percentile(all_lat, 99.9));
    }
    printf("└────────────────────────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP benchmark
// Payload format: [seq: int64][send_time_ns: int64][padding...]  (min 16 bytes)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int UDP_HDR = 16; // seq(8) + timestamp(8)

static void run_udp_bench(const char *addr, int udp_port,
                          int msg_size, int duration_s, int rate_pps)
{
    if (msg_size < UDP_HDR) {
        printf("[warn] UDP msg_size increased to %d (minimum for seq+ts header)\n",
               UDP_HDR);
        msg_size = UDP_HDR;
    }

    printf("\n┌── UDP echo benchmark ──────────────────────────────────────\n");
    printf("│  addr=%-15s port=%-6d msg_size=%-4d bytes\n",
           addr, udp_port, msg_size);
    printf("│  duration=%ds  target_rate=%s pps\n",
           duration_s,
           rate_pps == 0 ? "unlimited" : std::to_string(rate_pps).c_str());

    int sfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd < 0) { perror("udp socket"); return; }

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(static_cast<uint16_t>(udp_port));
    ::inet_pton(AF_INET, addr, &sa.sin_addr);
    ::connect(sfd, reinterpret_cast<sockaddr *>(&sa), sizeof sa);

    // Non-blocking
    int fl = ::fcntl(sfd, F_GETFL, 0);
    ::fcntl(sfd, F_SETFL, fl | O_NONBLOCK);

    std::vector<char> sbuf(msg_size, 'U');
    std::vector<char> rbuf(65536);

    // Sliding window: map seq → send_time_ns
    constexpr int WIN = 8192; // power-of-2
    static_assert((WIN & (WIN - 1)) == 0, "WIN must be power of 2");
    std::vector<long long> send_times(WIN, 0);

    long long seq_send     = 0;
    long long seq_expected = 0;
    long      sent_pkts    = 0;
    long      recv_pkts    = 0;
    long      lost_pkts    = 0;
    std::vector<long> latency_ns;
    latency_ns.reserve(2'000'000);

    const long long interval_ns =
        (rate_pps > 0) ? static_cast<long long>(1'000'000'000LL / rate_pps) : 0LL;

    auto deadline  = Clock::now() + std::chrono::seconds(duration_s);
    auto next_send = Clock::now();

    while (Clock::now() < deadline) {
        // ── send ────────────────────────────────────────────────────────────
        if (Clock::now() >= next_send) {
            long long seq = seq_send++;
            long long now_ns = Clock::now().time_since_epoch().count();
            memcpy(sbuf.data(),     &seq,    8);
            memcpy(sbuf.data() + 8, &now_ns, 8);

            if (::send(sfd, sbuf.data(), msg_size, 0) > 0) {
                send_times[seq & (WIN - 1)] = now_ns;
                ++sent_pkts;
            }
            if (interval_ns > 0)
                next_send += ns_t{interval_ns};
            else
                next_send = Clock::now();
        }

        // ── recv (drain as many as available) ───────────────────────────────
        for (;;) {
            int r = static_cast<int>(::recv(sfd, rbuf.data(), rbuf.size(), 0));
            if (r < UDP_HDR) break; // EAGAIN or short packet

            long long rseq = 0, rsend_ns = 0;
            memcpy(&rseq,     rbuf.data(),     8);
            memcpy(&rsend_ns, rbuf.data() + 8, 8);

            long long recv_ns = Clock::now().time_since_epoch().count();
            if (rsend_ns != 0)
                latency_ns.push_back(static_cast<long>(recv_ns - rsend_ns));

            ++recv_pkts;
            // Loss accounting
            if (rseq > seq_expected) {
                lost_pkts += rseq - seq_expected;
                seq_expected = rseq + 1;
            } else if (rseq == seq_expected) {
                ++seq_expected;
            }
            // (rseq < seq_expected: duplicate / reorder — ignore)
        }
    }

    ::close(sfd);

    double pps_sent = static_cast<double>(sent_pkts) / duration_s;
    double pps_recv = static_cast<double>(recv_pkts) / duration_s;
    double loss_pct = (sent_pkts > 0)
                    ? 100.0 * static_cast<double>(sent_pkts - recv_pkts) /
                              static_cast<double>(sent_pkts)
                    : 0.0;

    printf("│\n");
    printf("│  Sent:          %10.0f pps  (%ld total)\n", pps_sent, sent_pkts);
    printf("│  Received:      %10.0f pps  (%ld total)\n", pps_recv, recv_pkts);
    printf("│  Packet loss:   %10.2f%%\n", loss_pct);
    if (!latency_ns.empty()) {
        printf("│  RTT latency:\n");
        printf("│    avg  = %8.1f µs\n", avg_us(latency_ns));
        printf("│    p50  = %8.1f µs\n", percentile(latency_ns, 50.0));
        printf("│    p90  = %8.1f µs\n", percentile(latency_ns, 90.0));
        printf("│    p99  = %8.1f µs\n", percentile(latency_ns, 99.0));
        printf("│    p999 = %8.1f µs\n", percentile(latency_ns, 99.9));
    }
    printf("└────────────────────────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char **argv)
{
    const char *addr = "127.0.0.1";
    int  tcp_port    = 8080;
    int  udp_port    = 8081;
    int  conns       = 100;
    int  tcp_size    = 64;
    int  udp_size    = 64;
    int  duration    = 5;
    int  udp_rate    = 200'000;
    bool do_tcp      = true;
    bool do_udp      = true;
    bool do_sweep    = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--addr"     && i+1<argc) addr      = argv[++i];
        else if (a == "--tcp-port" && i+1<argc) tcp_port  = atoi(argv[++i]);
        else if (a == "--udp-port" && i+1<argc) udp_port  = atoi(argv[++i]);
        else if (a == "--conns"    && i+1<argc) conns     = atoi(argv[++i]);
        else if (a == "--size"     && i+1<argc) tcp_size = udp_size = atoi(argv[++i]);
        else if (a == "--tcp-size" && i+1<argc) tcp_size  = atoi(argv[++i]);
        else if (a == "--udp-size" && i+1<argc) udp_size  = atoi(argv[++i]);
        else if (a == "--duration" && i+1<argc) duration  = atoi(argv[++i]);
        else if (a == "--udp-rate" && i+1<argc) udp_rate  = atoi(argv[++i]);
        else if (a == "--no-tcp")               do_tcp    = false;
        else if (a == "--no-udp")               do_udp    = false;
        else if (a == "--no-sweep")             do_sweep  = false;
        else if (a == "-h" || a == "--help") {
            printf(
                "Usage: bench [options]\n"
                "  --addr ADDR        server IP          (default: 127.0.0.1)\n"
                "  --tcp-port PORT    TCP port           (default: 8080)\n"
                "  --udp-port PORT    UDP port           (default: 8081)\n"
                "  --conns N          TCP connections    (default: 100)\n"
                "  --size N           msg size (TCP+UDP) (default: 64)\n"
                "  --tcp-size N       TCP msg size\n"
                "  --udp-size N       UDP msg size       (min 16)\n"
                "  --duration S       seconds per test   (default: 5)\n"
                "  --udp-rate N       UDP pps target     (default: 200000, 0=unlimited)\n"
                "  --no-tcp           skip TCP tests\n"
                "  --no-udp           skip UDP test\n"
                "  --no-sweep         skip TCP scaling sweep\n");
            return 0;
        }
    }

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         Proactor-Server Performance Benchmark                ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    if (do_tcp) {
        run_tcp_bench(addr, tcp_port, conns, tcp_size, duration);
        if (do_sweep)
            run_tcp_scaling(addr, tcp_port, tcp_size, duration);
    }
    if (do_udp)
        run_udp_bench(addr, udp_port, udp_size, duration, udp_rate);

    printf("\nBenchmark complete.\n");
    return 0;
}
