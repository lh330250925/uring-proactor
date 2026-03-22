# uring-proactor

**[English](#english) · [中文](#中文)**

---

<a name="english"></a>

A high-performance C++23 asynchronous TCP/UDP server framework built on **Linux io_uring** using the **Proactor** pattern. All I/O operations are submitted to the kernel ring and completed via coroutines, eliminating blocking syscalls and minimising context switches.

## Features

- **io_uring** driven — uses `IORING_OP_ACCEPT`, `IORING_OP_RECV`, `IORING_OP_SEND`, `IORING_OP_RECVMSG`, `IORING_OP_SENDMSG`
- **Proactor pattern** — coroutines suspend on I/O submission and resume on completion; no manual epoll loops
- **Multi-threaded** — one `IoRing` per thread, no cross-thread locking for the hot path
- **SQPOLL support** — optional kernel polling thread per ring (`IORING_SETUP_SQPOLL`) eliminates submit syscalls
- **Zero-copy UDP receive** — io_uring buffer rings with multi-shot `recvmsg`
- **Buffer pool** — fixed-size slab allocator shared across worker threads
- **TCP channel pool** — per-thread pooling of `TcpChannel` objects
- **C++23** — coroutines (`co_await`), `std::jthread`, `std::stop_token`

## Performance (loopback, normal mode)

| Test | Throughput | MB/s | p50 | p99 |
|:---|---:|---:|---:|---:|
| TCP 1 conn, 64 B | 457,925 msg/s | 59 | **2.1 µs** | **3.0 µs** |
| TCP 10 conns, 64 B | 2,169,651 msg/s | 278 | 2.9 µs | 19.6 µs |
| TCP 50 conns, 64 B | **2,504,898 msg/s** | 321 | 15.5 µs | 58.7 µs |
| TCP 100 conns, 64 B | 2,400,108 msg/s | 307 | 34.1 µs | 109.4 µs |
| TCP 500 conns, 64 B | 2,129,285 msg/s | 273 | 161.9 µs | 640.1 µs |
| TCP 100 conns, 4096 B | 1,620,422 msg/s | 13,275 | 52.5 µs | 183.6 µs |
| UDP 200K pps, 64 B | 199,522 pps recv | — | 7.9 µs | 19.9 µs |
| UDP unlimited, 64 B | **986,221 pps** recv | — | 9.7 µs | 29.7 µs |

Environment: 10-core ARM container (OrbStack), Linux 6.17.8, Release build.  
See [BENCHMARK.md](BENCHMARK.md) for full results and methodology.

## Requirements

| Dependency | Version |
|------------|---------|
| Linux kernel | ≥ 5.19 (6.x recommended for full feature set) |
| liburing | ≥ 2.3 |
| libnuma | any |
| g++ | ≥ 13 (C++23) |

```bash
# Ubuntu / Debian
apt install -y liburing-dev libnuma-dev g++ build-essential
```

## Build

### CMake (recommended)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Binaries
./build/echo_server
./build/bench
```

### Manual g++

```bash
g++ -std=c++23 -O3 -I include \
    -o echo_server \
    test/echo_server.cpp \
    src/core/io_thread.cpp \
    src/core/tcp_thread.cpp \
    src/core/udp_thread.cpp \
    src/core/awaiter.cpp \
    src/core/io_ring.cpp \
    src/core/buf_ring.cpp \
    src/net/server.cpp \
    src/net/channel.cpp \
    src/net/handler.cpp \
    src/net/acceptor.cpp \
    src/net/tcp_channel_pool.cpp \
    src/net/socket.cpp \
    src/net/ip_address.cpp \
    src/buffer/buffer.cpp \
    src/buffer/buf_pool.cpp \
    src/buffer/msghdr_pool.cpp \
    src/util/log.cpp \
    src/util/timestamp.cpp \
    -luring -lnuma
```

## Quick Start

### TCP echo server

```cpp
#include "net/server.hpp"
#include "net/channel.hpp"

class MyTcpServer : public TcpServer
{
public:
    MyTcpServer()
        : TcpServer(/*port=*/8080, /*threads=*/4,
                    /*buf_pool_size=*/1024,
                    /*channel_capacity=*/64,
                    /*buf_ring_size=*/512,
                    /*pool_size=*/256) {}

    void on_read(TcpChannel &ch) override
    {
        while (true)
        {
            auto *res = ch.peek(1);
            if (!res || res->count == 0) break;
            for (unsigned i = 0; i < res->count; ++i)
                ch.append(res->data[i], res->size[i]);
            unsigned total = 0;
            for (unsigned i = 0; i < res->count; ++i)
                total += res->size[i];
            ch.consume(total);
        }
        ch.submit();
    }

    void on_accept(TcpChannel &ch) override { /* new connection */ }
    void on_close(TcpChannel &ch, int error) override { /* disconnected */ }
};

int main()
{
    MyTcpServer server;
    server.start();         // starts worker threads and blocks
}
```

### UDP echo server

```cpp
#include "net/server.hpp"
#include "net/channel.hpp"

class MyUdpServer : public UdpServer
{
public:
    MyUdpServer()
        : UdpServer(/*port=*/8081, /*threads=*/4,
                    /*buf_pool_size=*/1024,
                    /*buf_ring_size=*/512,
                    /*msghdr_pool_size=*/64,
                    /*channel_capacity=*/64) {}

    void on_read(UdpChannel &ch) override
    {
        while (true)
        {
            DgramMsg *msg = ch.read_buf_.peek();
            if (!msg) break;
            ch.append(msg->payload(), msg->payload_length());
            ch.submit(msg->peer_addr(), msg->peer_namelen());
            ch.consume();
        }
    }
};
```

### SQPOLL mode

Pass `cpu_affinity = true` to the server constructors and run as root:

```bash
sudo ./echo_server --sqpoll
```

Each worker thread pair pins to two adjacent CPUs; the kernel SQPOLL thread on the second CPU polls the submission ring without syscalls.

## Project Structure

```
include/
  buffer/     — BufPool, BufRing, ReadBuf, WriteBuf, DgramMsg
  core/       — IoRing, IoThread, TcpThread, UdpThread, Awaiter
  net/        — Server, TcpServer, UdpServer, TcpChannel, UdpChannel
  util/       — logging, timestamp, MPSC queue
src/
  buffer/     — implementation
  core/       — implementation
  net/        — implementation
  util/       — implementation
test/
  echo_server.cpp  — TCP + UDP echo server example
  bench.cpp        — latency / throughput benchmark tool
```

## Architecture

```
Application code (subclass TcpServer / UdpServer)
         │ on_accept / on_read / on_close callbacks
         ▼
    TcpServer / UdpServer
         │ owns N threads
         ▼
    TcpThread / UdpThread   (one per core)
         │ owns
         ├─ IoRing           (io_uring instance)
         ├─ BufRing          (kernel-managed receive buffer ring)
         ├─ BufPool          (user-space buffer slab)
         └─ TcpChannelPool / UdpChannel

    Coroutines (C++23 co_await)
         tcp_read_loop / tcp_write_loop
         udp_read_loop / udp_write_loop
         accept_loop
              │ use Sqe (io_uring SQE wrapper)
              │ suspend on submission
              └─ resume via Token::complete() on CQE
```

## Benchmark Tool

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# TCP: 10 connections, 5 s
./build/bench --no-sweep --conns 10 --duration 5

# UDP: 200k pps
./build/bench --no-tcp --udp-rate 200000 --duration 5

# Full suite (auto-generates BENCHMARK.md)
bash run_bench.sh
```

See [BENCHMARK.md](BENCHMARK.md) for full results and methodology.

## Contributing

Issues and pull requests are welcome. Please:
- Target Linux kernel ≥ 5.19 (6.x recommended)
- Run `cmake --build build` and verify no warnings before submitting
- Keep the zero-mutex hot-path invariant

## License

MIT

---

<a name="中文"></a>

# uring-proactor（中文文档）

基于 **Linux io_uring** 和 **Proactor 模式**的高性能 C++23 异步 TCP/UDP 服务器框架。所有 I/O 操作通过内核环提交、经协程完成，消除阻塞系统调用，最小化上下文切换。

## 特性

- **io_uring 驱动** — 使用 `IORING_OP_ACCEPT`、`IORING_OP_RECV`、`IORING_OP_SEND`、`IORING_OP_RECVMSG`、`IORING_OP_SENDMSG`
- **Proactor 模式** — 协程在 I/O 提交时挂起，完成时恢复；无手动 epoll 循环
- **多线程无锁热路径** — 每个线程独占一个 `IoRing`，热路径零互斥锁
- **SQPOLL 支持** — 可选内核轮询线程（`IORING_SETUP_SQPOLL`），消除提交系统调用
- **零拷贝 UDP 接收** — io_uring 缓冲区环 + multishot `recvmsg`
- **缓冲池** — 固定大小 slab 分配器，write buffer 零堆分配
- **TCP 连接池** — 每线程预分配 `TcpChannel` 对象，复用于新连接
- **C++23 协程** — `co_await`、`std::jthread`、`std::stop_token`
- **异步双缓冲日志** — 每线程 2×32 MB 缓冲，io_uring 后台写文件，热路径零分配

## 性能指标（回环网络，普通模式）

| 测试场景 | 吞吐量 | 带宽 | p50 | p99 |
|:---|---:|---:|---:|---:|
| TCP 1 连接，64 B | 457,925 msg/s | 59 MB/s | **2.1 µs** | **3.0 µs** |
| TCP 10 连接，64 B | 2,169,651 msg/s | 278 MB/s | 2.9 µs | 19.6 µs |
| TCP 50 连接，64 B | **2,504,898 msg/s** | 321 MB/s | 15.5 µs | 58.7 µs |
| TCP 100 连接，64 B | 2,400,108 msg/s | 307 MB/s | 34.1 µs | 109.4 µs |
| TCP 500 连接，64 B | 2,129,285 msg/s | 273 MB/s | 161.9 µs | 640.1 µs |
| TCP 100 连接，4096 B | 1,620,422 msg/s | 13,275 MB/s | 52.5 µs | 183.6 µs |
| UDP 200K pps，64 B | 199,522 pps 接收 | — | 7.9 µs | 19.9 µs |
| UDP 无限速率，64 B | **986,221 pps** 接收 | — | 9.7 µs | 29.7 µs |

测试环境：10 核 ARM 容器（OrbStack），Linux 6.17.8，Release 构建。详情见 [BENCHMARK.md](BENCHMARK.md)。

## 环境要求

| 依赖 | 版本 |
|:---|:---|
| Linux 内核 | ≥ 5.19（推荐 6.x） |
| liburing | ≥ 2.3 |
| libnuma | 任意版本 |
| g++ | ≥ 13（需要 C++23） |

```bash
# Ubuntu / Debian
apt install -y liburing-dev libnuma-dev g++ build-essential cmake
```

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 可执行文件
./build/echo_server       # 示例 echo 服务器
./build/bench             # 性能测试工具
```

## 快速开始

### TCP Echo 服务器

```cpp
#include "net/server.hpp"
#include "net/channel.hpp"

class MyServer : public TcpServer {
public:
    MyServer() : TcpServer(8080, /*threads=*/4) {}

    void on_read(TcpChannel &ch) override {
        while (true) {
            auto *res = ch.peek(1);
            if (!res || res->count == 0) break;
            for (unsigned i = 0; i < res->count; ++i)
                ch.append(res->data[i], res->size[i]);
            unsigned total = 0;
            for (unsigned i = 0; i < res->count; ++i) total += res->size[i];
            ch.consume(total);
        }
        ch.submit();
    }

    void on_accept(TcpChannel &) override {}
    void on_close(TcpChannel &, int) override {}
};

int main() {
    MyServer server;
    server.start();   // 启动工作线程并阻塞
}
```

### UDP Echo 服务器

```cpp
#include "net/server.hpp"
#include "net/channel.hpp"

class MyUdpServer : public UdpServer {
public:
    MyUdpServer() : UdpServer(8081, /*threads=*/4) {}

    void on_read(UdpChannel &ch) override {
        while (true) {
            DgramMsg *msg = ch.read_buf_.peek();
            if (!msg) break;
            ch.append(msg->payload(), msg->payload_length());
            ch.submit(msg->peer_addr(), msg->peer_namelen());
            ch.consume();
        }
    }
};
```

### SQPOLL 模式

以 root 权限运行，使用 `--sqpoll` 参数：

```bash
sudo ./build/echo_server --sqpoll
```

每对工作线程绑定到两个相邻 CPU 核；内核 SQPOLL 线程在第二个 CPU 上无系统调用地轮询提交队列。

> **注意：** SQPOLL 模式在 bench 和 server 共享同一物理机时可能因 CPU 竞争导致吞吐下降。推荐在 server/client 分机部署时启用。

## 项目结构

```
include/
  buffer/     — BufPool、BufRing、ReadBuf、WriteBuf、DgramMsg
  core/       — IoRing、IoThread、TcpThread、UdpThread、Awaiter
  net/        — Server、TcpServer、UdpServer、TcpChannel、UdpChannel
  util/       — 日志、时间戳、MPSC 队列
src/
  buffer/     — 实现
  core/       — 实现
  net/        — 实现
  util/       — 实现
test/
  echo_server.cpp  — TCP + UDP echo 示例
  bench.cpp        — 延迟/吞吐测试工具
run_bench.sh       — 自动化测试脚本，生成 BENCHMARK.md
```

## 架构概览

```
用户代码（继承 TcpServer / UdpServer）
         │ on_accept / on_read / on_close 回调
         ▼
    TcpServer / UdpServer（统筹 N 个线程）
         │
         ▼
    TcpThread / UdpThread（每核一个线程）
         ├─ IoRing           ← io_uring 实例
         ├─ BufRing          ← 内核管理的接收缓冲区环
         ├─ BufPool          ← 用户空间写缓冲 slab
         └─ TcpChannelPool   ← 预分配连接对象池

    C++23 协程
         accept_loop / tcp_read_loop / tcp_write_loop
              │  提交 SQE → 挂起
              └─ CQE 触发 Token::complete() → 恢复
```

## 贡献

欢迎 Issue 和 Pull Request。提交前请：

- 确保目标内核 ≥ 5.19（推荐 6.x）
- 执行 `cmake --build build` 确认无编译错误
- 保持热路径零互斥锁的设计原则

## 许可证 / License

MIT
