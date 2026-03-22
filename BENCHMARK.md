# uring-proactor 性能测试报告 / Performance Benchmark Report

**[English](#english) · [中文](#中文)**

---

<a name="english"></a>

## Test Environment

| Item | Value |
|:---|:---|
| Date | 2026-03-22 |
| OS | Linux 6.17.8-orbstack (arm64) |
| CPU | Apple Silicon (ARM Cortex, 10 cores @ 2000 MHz) |
| CPU Cores | 10 |
| Build Type | Release (`-O3`) |
| Server Threads | 10 (`hardware_concurrency()`) |
| io_uring Mode | Normal Mode |
| Duration per Point | 8 s (median of 3 independent runs) |
| Methodology | Ping-pong (1 RTT per measurement), loopback 127.0.0.1 |

> All tests run on loopback on the same host; latency includes the full kernel TCP/UDP stack round-trip.

---

## 1. TCP Baseline Latency (1 Connection, 64 B)

> Measures minimum achievable RTT with zero scheduler or queue contention.

| Metric | Value |
|:---|---:|
| Throughput | 457,925 msg/s |
| Bandwidth | 58.61 MB/s |
| avg RTT | 2.2 µs |
| **p50 RTT** | **2.1 µs** |
| p90 RTT | 2.4 µs |
| **p99 RTT** | **3.0 µs** |
| p99.9 RTT | 11.7 µs |
| max RTT | 3,197.5 µs |

---

## 2. TCP Concurrency Scaling (64 B)

> Fixed 64-byte messages, varying concurrent connections. Each connection thread runs ping-pong independently.

| conns | QPS | MB/s | avg µs | p50 µs | p90 µs | p99 µs | p99.9 µs | max µs |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 454,599 | 58.19 | 2.2 | 2.1 | 2.4 | 2.8 | 11.3 | 2,727.8 |
| 10 | 2,169,651 | 277.72 | 4.5 | 2.9 | 6.9 | 19.6 | 32.8 | 5,869.8 |
| 50 | **2,504,898** | **320.63** | 19.9 | 15.5 | 36.8 | 58.7 | 142.5 | 16,428.8 |
| 100 | 2,400,108 | 307.21 | 41.6 | 34.1 | 71.0 | 109.4 | 295.2 | 28,726.5 |
| 200 | 2,253,307 | 288.42 | 88.7 | 71.8 | 144.2 | 209.9 | 844.0 | 40,118.2 |
| 500 | 2,129,285 | 272.55 | 234.6 | 161.9 | 409.5 | 640.1 | 1,637.6 | 51,907.9 |

**Peak: 2,504,898 msg/s at 50 concurrent connections.**

---

## 3. TCP Message Size Impact (100 Connections)

> Fixed 100 concurrent connections, varying payload size.

| msg size | QPS | MB/s | p50 µs | p90 µs | p99 µs | p99.9 µs |
|---:|---:|---:|---:|---:|---:|---:|
| 64 B | 2,319,814 | 296.94 | 37.7 | 75.4 | 109.5 | 324.3 |
| 256 B | 2,268,855 | 1,161.65 | 37.7 | 71.2 | 107.6 | 357.9 |
| 1024 B | 2,067,297 | 4,233.82 | 41.9 | 81.1 | 126.5 | 388.9 |
| 4096 B | 1,620,422 | 13,274.50 | 52.5 | 109.6 | 183.6 | 609.3 |

---

## 4. UDP Performance (64 B)

> Single sender thread, varying target send rate.

| target (pps) | sent pps | recv pps | loss | p50 µs | p99 µs |
|---:|---:|---:|---:|---:|---:|
| 100,000 | 100,000 | 99,934 | 0.07% | 7.6 | 17.5 |
| 200,000 | 200,000 | 199,522 | **0.24%** | 7.9 | 19.9 |
| 500,000 | 500,000 | 497,298 | 0.54% | 6.0 | 16.1 |
| unlimited | 990,177 | **986,221** | 0.40% | 9.7 | 29.7 |

---

## Summary

| Key Metric | Value |
|:---|:---|
| Peak TCP Throughput | **2,504,898 msg/s** (50 conns, 64B) |
| Min TCP p50 RTT | **2.1 µs** (1 conn, 64B) |
| Min TCP p99 RTT | **3.0 µs** (1 conn, 64B) |
| p99/p50 at baseline | 1.4× |
| QPS at 500 conns vs peak | **85%** |
| Peak UDP Receive | **~986,221 pps** |
| UDP p99 at 200K pps | **19.9 µs** |
| Hot-Path Mutexes | **0** |

---

## Methodology Notes

- All tests run on **loopback (127.0.0.1)**; bench and server share the same physical host.
- TCP uses **ping-pong mode**: each connection thread sends one message and blocks waiting for echo; each RTT is individually timed.
- Each data point is the **median of 3 independent runs** (8 s per point per run).
- Each test group is independently timed; the server is **not restarted** between data points within a run.
- The bench spawns one thread per connection; each thread owns a dedicated socket.
- UDP latency is measured by embedding a send timestamp in the payload and computing the difference on receipt.
- To regenerate: `bash run_bench.sh`

---
---

<a name="中文"></a>

## 测试环境

| 项目 | 值 |
|:---|:---|
| 日期 | 2026-03-22 |
| 操作系统 | Linux 6.17.8-orbstack（arm64） |
| CPU | Apple Silicon（ARM Cortex，10 核，2000 MHz） |
| CPU 核心数 | 10 |
| 构建类型 | Release（`-O3`） |
| 服务器线程数 | 10（= `hardware_concurrency()`） |
| io_uring 模式 | 普通模式 |
| 每测试点时长 | 8 秒（3 次独立运行取中位数） |
| 测试方法 | Ping-pong（每次测量 1 个 RTT），回环地址 127.0.0.1 |

> 所有测试均在同一台机器的回环接口上运行，延迟包含完整的内核 TCP/UDP 栈往返开销。

---

## 一、TCP 单连接基准（1 连接，64 字节）

> 无并发竞争场景下的最低可达 RTT。

| 指标 | 值 |
|:---|---:|
| 吞吐量 | 457,925 msg/s |
| 带宽 | 58.61 MB/s |
| 平均 RTT | 2.2 µs |
| **p50 RTT** | **2.1 µs** |
| p90 RTT | 2.4 µs |
| **p99 RTT** | **3.0 µs** |
| p99.9 RTT | 11.7 µs |
| 最大 RTT | 3,197.5 µs |

---

## 二、TCP 并发扩展（64 字节）

> 固定 64 字节消息，改变并发连接数。每个连接线程独立运行 ping-pong。

| 并发连接 | QPS | 带宽 MB/s | avg µs | p50 µs | p90 µs | p99 µs | p99.9 µs | max µs |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 454,599 | 58.19 | 2.2 | 2.1 | 2.4 | 2.8 | 11.3 | 2,727.8 |
| 10 | 2,169,651 | 277.72 | 4.5 | 2.9 | 6.9 | 19.6 | 32.8 | 5,869.8 |
| 50 | **2,504,898** | **320.63** | 19.9 | 15.5 | 36.8 | 58.7 | 142.5 | 16,428.8 |
| 100 | 2,400,108 | 307.21 | 41.6 | 34.1 | 71.0 | 109.4 | 295.2 | 28,726.5 |
| 200 | 2,253,307 | 288.42 | 88.7 | 71.8 | 144.2 | 209.9 | 844.0 | 40,118.2 |
| 500 | 2,129,285 | 272.55 | 234.6 | 161.9 | 409.5 | 640.1 | 1,637.6 | 51,907.9 |

**峰值：2,504,898 msg/s（50 并发连接）**

---

## 三、TCP 消息大小影响（100 并发连接）

> 固定 100 并发连接，改变有效负载大小。

| 消息大小 | QPS | 带宽 MB/s | p50 µs | p90 µs | p99 µs | p99.9 µs |
|---:|---:|---:|---:|---:|---:|---:|
| 64 B | 2,319,814 | 296.94 | 37.7 | 75.4 | 109.5 | 324.3 |
| 256 B | 2,268,855 | 1,161.65 | 37.7 | 71.2 | 107.6 | 357.9 |
| 1024 B | 2,067,297 | 4,233.82 | 41.9 | 81.1 | 126.5 | 388.9 |
| 4096 B | 1,620,422 | 13,274.50 | 52.5 | 109.6 | 183.6 | 609.3 |

---

## 四、UDP 性能（64 字节）

> 单发送线程，改变目标发送速率。

| 目标速率 (pps) | 发送 pps | 接收 pps | 丢包率 | p50 µs | p99 µs |
|---:|---:|---:|---:|---:|---:|
| 100,000 | 100,000 | 99,934 | 0.07% | 7.6 | 17.5 |
| 200,000 | 200,000 | 199,522 | **0.24%** | 7.9 | 19.9 |
| 500,000 | 500,000 | 497,298 | 0.54% | 6.0 | 16.1 |
| 无限 | 990,177 | **986,221** | 0.40% | 9.7 | 29.7 |

---

## 综合摘要

| 关键指标 | 值 |
|:---|:---|
| TCP 峰值吞吐 | **2,504,898 msg/s**（50 并发，64B） |
| TCP 最低 p50 RTT | **2.1 µs**（1 连接，64B） |
| TCP 最低 p99 RTT | **3.0 µs**（1 连接，64B） |
| 基准 p99/p50 比 | 1.4× |
| 500 并发 QPS 保持率 | **85%** |
| UDP 峰值接收速率 | **~986,221 pps** |
| UDP p99（200K pps） | **19.9 µs** |
| 热路径互斥锁数量 | **0** |

---

## 测试方法说明

- 所有测试均在**回环接口（127.0.0.1）**上运行，bench 与 server 共享同一物理主机。
- TCP 采用 **ping-pong 模式**：每个连接线程发送一条消息后阻塞等待 echo，每次 RTT 单独计时。
- 每个数据点为 **3 次独立运行的中位数**（8 秒/点/次）。
- 每组测试独立计时，单次运行内 server **不重启**。
- bench 端每个连接分配一个独立线程，每线程独占一个 socket。
- UDP 延迟通过在 payload 中嵌入发送时间戳并在接收时计算差值来测量。
- 重新生成报告：`bash run_bench.sh`
