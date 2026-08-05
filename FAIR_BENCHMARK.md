# Fair Cross-Platform Benchmark Methodology

Use `run_bench_fair.sh` when comparing this server across machines. The regular
`run_bench.sh` explores the maximum capacity of the current host; it intentionally
uses all available workers and is therefore not a controlled hardware comparison.

## Controlled Variables

- Release build with the same compiler family and comparable optimization flags.
- Four server workers on four dedicated CPUs.
- Client processes on four separate CPUs.
- One warmup pass before measurement.
- Three 10-second samples per point; report the median and the full range.
- Identical connection counts, message sizes, UDP rates, and io_uring mode.
- AC power, performance governor, idle desktop, and no thermal throttling.

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
bash run_bench_fair.sh
```

Optional overrides:

```bash
SERVER_CPUS=0,2,4,6 CLIENT_CPUS=1,3,5,7 SERVER_THREADS=4 \
BENCH_DURATION=10 BENCH_REPEATS=3 bash run_bench_fair.sh
```

The two CPU sets must not overlap. The script defaults to the eight online CPUs
with the highest advertised maximum frequency and interleaves them between server
and client. Override both sets when comparing hybrid processors whose core classes
cannot be matched across the two systems.

## Test Classes

1. TCP latency: one connection and 64-byte messages. This measures the complete
   client/kernel/server loopback RTT and is not a server-only latency number.
2. TCP throughput: 100 connections at 64 bytes with a fixed CPU budget.
3. TCP large-message throughput: 100 connections at 4096 bytes with the same
   fixed CPU budget.
4. UDP controlled load: 200K pps. Latency is valid only if loss stays below 1%.
5. UDP capacity: four unlimited senders. Compare receive rate and loss, not latency.

For publication-quality server-only throughput, run the client on a second physical
machine connected through the same-speed NIC and switch. Keep this local suite for
regression testing and CPU/kernel path comparisons.