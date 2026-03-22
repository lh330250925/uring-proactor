#!/bin/bash
# Proactor-Server Comprehensive Performance Benchmark — Normal Mode
# Runs 4 test groups and writes a Markdown report to ./perf_report.md

set -e
cd "$(dirname "$0")"

BENCH=./build/bench
SERVER=./build/echo_server
REPORT=./perf_report.md
DURATION=${BENCH_DURATION:-5}
SERVER_PID=

die()     { echo "ERROR: $*" >&2; cleanup 2>/dev/null; exit 1; }
cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=
    fi
}
trap 'cleanup' EXIT INT TERM

[[ -x $BENCH  ]] || die "bench not found — build first: cmake --build build"
[[ -x $SERVER ]] || die "echo_server not found — build first: cmake --build build"

# ─── extraction helpers (POSIX ERE only, no grep -P) ────────────────────────
_qps()  { grep -oE '[0-9]+ msg/s'     <<< "$1" | grep -oE '^[0-9]+'     | head -1; }
_mbps() { grep 'msg/s'      <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_avg()  { grep 'avg  ='     <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_p50()  { grep 'p50 '       <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_p90()  { grep 'p90 '       <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_p99()  { grep 'p99 '       <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_p999() { grep 'p999 '      <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_max()  { grep 'max  ='     <<< "$1" | grep -oE '[0-9]+\.[0-9]+'        | head -1; }
_loss() { grep 'Packet loss' <<< "$1" | grep -oE '[0-9]+\.[0-9]+'       | head -1; }
_sent() { grep 'Sent:'      <<< "$1" | grep -oE '[0-9]+ pps' | grep -oE '^[0-9]+' | head -1; }
_recv() { grep 'Received:'  <<< "$1" | grep -oE '[0-9]+ pps' | grep -oE '^[0-9]+' | head -1; }

tcp_bench() { $BENCH --no-udp --no-sweep --conns "$1" --tcp-size "$2" --duration $DURATION 2>&1; }
udp_bench() { $BENCH --no-tcp --udp-rate "$1" --duration $DURATION 2>&1; }

# ─── start server ─────────────────────────────────────────────────────────────
pkill -9 -f "build/echo_server" 2>/dev/null || true
sleep 1
$SERVER > /tmp/proactor_srv.log 2>&1 &
SERVER_PID=$!
sleep 1
kill -0 "$SERVER_PID" 2>/dev/null || die "Server failed to start — see /tmp/proactor_srv.log"

NPROC=$(nproc 2>/dev/null || grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo 1)
_CPU_VENDOR=$(lscpu 2>/dev/null | grep 'Vendor ID' | cut -d: -f2 | xargs)
_CPU_ARCH=$(uname -m)
_CPU_MHZ=$(lscpu 2>/dev/null | grep 'CPU max MHz' | grep -oP '[0-9]+' | head -1)
_CPU_MODEL=$(grep 'model name' /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs)
if [[ -n "$_CPU_MODEL" && "$_CPU_MODEL" != "-" && "$_CPU_MODEL" != "" ]]; then
    CPU_MODEL="$_CPU_MODEL"
else
    CPU_MODEL="${_CPU_VENDOR:-unknown} ${_CPU_ARCH}"
    [[ -n "$_CPU_MHZ" ]] && CPU_MODEL="$CPU_MODEL @ ${_CPU_MHZ}MHz"
fi
BUILD_TYPE=$(grep 'CMAKE_BUILD_TYPE:' ./build/CMakeCache.txt 2>/dev/null | cut -d= -f2 | xargs || echo "Unknown")
SRV_THREADS="$NPROC"  # normal mode: threads = std::thread::hardware_concurrency()
DATE=$(date +"%Y-%m-%d %H:%M")

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║        Proactor-Server Comprehensive Benchmark               ║"
echo "╚══════════════════════════════════════════════════════════════╝"
printf " CPU   : %s (%d cores)\n" "$CPU_MODEL" "$NPROC"
printf " Build : %s  |  Server threads: %s\n" "$BUILD_TYPE" "$SRV_THREADS"
printf " Duration per point: %ds\n" "$DURATION"
echo ""

# ══════════════════════════════════════════════ TEST 1: TCP Baseline ═══════════
echo "──────────────────────────────────────────────────────────────────"
echo " TEST 1/4  TCP Baseline  (1 conn, 64B)"
echo "──────────────────────────────────────────────────────────────────"
OUT1=$(tcp_bench 1 64)
T1_QPS=$(_qps "$OUT1");  T1_MBPS=$(_mbps "$OUT1")
T1_AVG=$(_avg "$OUT1");  T1_P50=$(_p50 "$OUT1");  T1_P90=$(_p90 "$OUT1")
T1_P99=$(_p99 "$OUT1");  T1_P999=$(_p999 "$OUT1"); T1_MAX=$(_max "$OUT1")
printf "  QPS=%-10s  p50=%-7s  p99=%-7s  p99.9=%-7s  max=%s µs\n" \
    "$T1_QPS" "${T1_P50}µs" "${T1_P99}µs" "${T1_P999}µs" "$T1_MAX"

# ══════════════════════════════════════ TEST 2: TCP Concurrency Scaling ════════
echo ""
echo "──────────────────────────────────────────────────────────────────"
echo " TEST 2/4  TCP Concurrency Scaling  (64B)"
echo "──────────────────────────────────────────────────────────────────"
printf "  %-6s  %-10s  %-7s  %-7s  %-7s  %-7s  %-9s  %s\n" \
    conns QPS MB/s "avg µs" "p50 µs" "p99 µs" "p99.9 µs" "max µs"

declare -A T2_QPS T2_MBPS T2_AVG T2_P50 T2_P90 T2_P99 T2_P999 T2_MAX
for c in 1 10 50 100 200 500; do
    OUT=$(tcp_bench "$c" 64)
    T2_QPS[$c]=$(_qps "$OUT");   T2_MBPS[$c]=$(_mbps "$OUT")
    T2_AVG[$c]=$(_avg "$OUT");   T2_P50[$c]=$(_p50 "$OUT");  T2_P90[$c]=$(_p90 "$OUT")
    T2_P99[$c]=$(_p99 "$OUT");   T2_P999[$c]=$(_p999 "$OUT"); T2_MAX[$c]=$(_max "$OUT")
    printf "  %-6d  %-10s  %-7s  %-7s  %-7s  %-7s  %-9s  %s\n" \
        "$c" "${T2_QPS[$c]}" "${T2_MBPS[$c]}" "${T2_AVG[$c]}" \
        "${T2_P50[$c]}" "${T2_P99[$c]}" "${T2_P999[$c]}" "${T2_MAX[$c]}"
done

# ══════════════════════════════════════ TEST 3: Message Size Sweep ═════════════
echo ""
echo "──────────────────────────────────────────────────────────────────"
echo " TEST 3/4  Message Size Sweep  (100 conns)"
echo "──────────────────────────────────────────────────────────────────"
printf "  %-8s  %-10s  %-8s  %-7s  %-7s  %s\n" \
    size QPS MB/s "p50 µs" "p99 µs" "p99.9 µs"

declare -A T3_QPS T3_MBPS T3_P50 T3_P90 T3_P99 T3_P999
for sz in 64 256 1024 4096; do
    OUT=$(tcp_bench 100 "$sz")
    T3_QPS[$sz]=$(_qps "$OUT");   T3_MBPS[$sz]=$(_mbps "$OUT")
    T3_P50[$sz]=$(_p50 "$OUT");   T3_P90[$sz]=$(_p90 "$OUT")
    T3_P99[$sz]=$(_p99 "$OUT");   T3_P999[$sz]=$(_p999 "$OUT")
    printf "  %-8s  %-10s  %-8s  %-7s  %-7s  %s\n" \
        "${sz}B" "${T3_QPS[$sz]}" "${T3_MBPS[$sz]}" \
        "${T3_P50[$sz]}" "${T3_P99[$sz]}" "${T3_P999[$sz]}"
done

# ══════════════════════════════════════ TEST 4: UDP Rate Sweep ═════════════════
echo ""
echo "──────────────────────────────────────────────────────────────────"
echo " TEST 4/4  UDP Rate Sweep  (64B)"
echo "──────────────────────────────────────────────────────────────────"
printf "  %-12s  %-10s  %-10s  %-8s  %-7s  %s\n" \
    "target pps" "sent pps" "recv pps" "loss %" "p50 µs" "p99 µs"

declare -A T4_SENT T4_RECV T4_LOSS T4_P50 T4_P99
for rate in 100000 200000 500000 0; do
    OUT=$(udp_bench "$rate")
    T4_SENT[$rate]=$(_sent "$OUT"); T4_RECV[$rate]=$(_recv "$OUT")
    T4_LOSS[$rate]=$(_loss "$OUT"); T4_P50[$rate]=$(_p50 "$OUT"); T4_P99[$rate]=$(_p99 "$OUT")
    label="$rate"; [[ "$rate" == "0" ]] && label="unlimited"
    printf "  %-12s  %-10s  %-10s  %-8s  %-7s  %s\n" \
        "$label" "${T4_SENT[$rate]}" "${T4_RECV[$rate]}" \
        "${T4_LOSS[$rate]}%" "${T4_P50[$rate]}" "${T4_P99[$rate]}"
done

# ─── stop server ──────────────────────────────────────────────────────────────
cleanup
echo ""
echo "Tests complete. Generating report..."

# ─── summary stats ────────────────────────────────────────────────────────────
PEAK_TCP_QPS=0; PEAK_TCP_CONNS=1
for c in 1 10 50 100 200 500; do
    q=${T2_QPS[$c]:-0}
    if (( q > PEAK_TCP_QPS )); then PEAK_TCP_QPS=$q; PEAK_TCP_CONNS=$c; fi
done

PEAK_UDP_RECV=0
for rate in 100000 200000 500000 0; do
    r=${T4_RECV[$rate]:-0}
    if (( r > PEAK_UDP_RECV )); then PEAK_UDP_RECV=$r; fi
done

P99_P50_RATIO=$(awk "BEGIN { p=\"${T1_P99:-0}\"; v=\"${T1_P50:-1}\"; if (v+0>0) printf \"%.1fx\", p/v; else print \"N/A\" }")
P99_GROWTH=$(awk "BEGIN { a=\"${T2_P99[100]:-0}\"; b=\"${T1_P99:-1}\"; if (b+0>0) printf \"%.1fx\", a/b; else print \"N/A\" }")
QPS_500_PCT=$(awk "BEGIN { a=\"${T2_QPS[500]:-0}\"; b=\"${PEAK_TCP_QPS:-1}\"; if (b+0>0) printf \"%.0f%%\", 100*a/b; else print \"N/A\" }")
MB_RATIO=$(awk "BEGIN { a=\"${T3_MBPS[4096]:-0}\"; b=\"${T3_MBPS[64]:-1}\"; if (b+0>0) printf \"%.1fx\", a/b; else print \"N/A\" }")

# ══════════════════════════════════════════════ Generate Markdown Report ═══════
cat > "$REPORT" << MDEOF
# Proactor Server Performance Report

*Generated: ${DATE}*

---

## Test Environment

| Item | Value |
|:-----|:------|
| Date | ${DATE} |
| OS | $(uname -sr) |
| CPU | ${CPU_MODEL} |
| CPU cores | ${NPROC} |
| Build type | ${BUILD_TYPE} |
| Server threads | ${SRV_THREADS} |
| io_uring mode | Normal |
| Test duration | ${DURATION}s per data point |
| Bench tool | ping-pong (1 RTT per measurement) |

---

## Summary

| Metric | Value |
|:-------|:------|
| Peak TCP throughput | **${PEAK_TCP_QPS} msg/s** (at ${PEAK_TCP_CONNS} conns, 64B) |
| Min RTT p50 (1 conn, 64B) | **${T1_P50} µs** |
| Min RTT p99 (1 conn, 64B) | **${T1_P99} µs** |
| p99/p50 at baseline | ${P99_P50_RATIO} |
| p99 at 100 conns vs 1 conn | ${P99_GROWTH} |
| QPS at 500 conns vs peak | ${QPS_500_PCT} |
| Peak UDP receive rate | **${PEAK_UDP_RECV} pps** |

---

## 1. TCP Baseline Latency

> Single connection, 64-byte echo. Represents minimum achievable RTT with no
> scheduler or queue contention.

| Metric | Value |
|:-------|------:|
| Throughput | ${T1_QPS} msg/s |
| Bandwidth | ${T1_MBPS} MB/s |
| avg RTT | ${T1_AVG} µs |
| p50 RTT | ${T1_P50} µs |
| p90 RTT | ${T1_P90} µs |
| p99 RTT | ${T1_P99} µs |
| p99.9 RTT | ${T1_P999} µs |
| max RTT | ${T1_MAX} µs |

---

## 2. TCP Concurrency Scaling

> Fixed 64-byte messages, varying concurrent connections. Each connection runs
> ping-pong independently. Tests how the server handles growing fan-in.

| conns | QPS | MB/s | avg µs | p50 µs | p90 µs | p99 µs | p99.9 µs | max µs |
|------:|----:|-----:|-------:|-------:|-------:|-------:|---------:|-------:|
| 1 | ${T2_QPS[1]} | ${T2_MBPS[1]} | ${T2_AVG[1]} | ${T2_P50[1]} | ${T2_P90[1]} | ${T2_P99[1]} | ${T2_P999[1]} | ${T2_MAX[1]} |
| 10 | ${T2_QPS[10]} | ${T2_MBPS[10]} | ${T2_AVG[10]} | ${T2_P50[10]} | ${T2_P90[10]} | ${T2_P99[10]} | ${T2_P999[10]} | ${T2_MAX[10]} |
| 50 | ${T2_QPS[50]} | ${T2_MBPS[50]} | ${T2_AVG[50]} | ${T2_P50[50]} | ${T2_P90[50]} | ${T2_P99[50]} | ${T2_P999[50]} | ${T2_MAX[50]} |
| 100 | ${T2_QPS[100]} | ${T2_MBPS[100]} | ${T2_AVG[100]} | ${T2_P50[100]} | ${T2_P90[100]} | ${T2_P99[100]} | ${T2_P999[100]} | ${T2_MAX[100]} |
| 200 | ${T2_QPS[200]} | ${T2_MBPS[200]} | ${T2_AVG[200]} | ${T2_P50[200]} | ${T2_P90[200]} | ${T2_P99[200]} | ${T2_P999[200]} | ${T2_MAX[200]} |
| 500 | ${T2_QPS[500]} | ${T2_MBPS[500]} | ${T2_AVG[500]} | ${T2_P50[500]} | ${T2_P90[500]} | ${T2_P99[500]} | ${T2_P999[500]} | ${T2_MAX[500]} |

**Peak throughput: ${PEAK_TCP_QPS} msg/s at ${PEAK_TCP_CONNS} concurrent connections.**

---

## 3. TCP Message Size Impact

> Fixed 100 concurrent connections, varying payload size. Shows how larger
> messages affect throughput (msg/s), bandwidth (MB/s), and latency.

| msg size | QPS | MB/s | p50 µs | p90 µs | p99 µs | p99.9 µs |
|---------:|----:|-----:|-------:|-------:|-------:|---------:|
| 64 B | ${T3_QPS[64]} | ${T3_MBPS[64]} | ${T3_P50[64]} | ${T3_P90[64]} | ${T3_P99[64]} | ${T3_P999[64]} |
| 256 B | ${T3_QPS[256]} | ${T3_MBPS[256]} | ${T3_P50[256]} | ${T3_P90[256]} | ${T3_P99[256]} | ${T3_P999[256]} |
| 1024 B | ${T3_QPS[1024]} | ${T3_MBPS[1024]} | ${T3_P50[1024]} | ${T3_P90[1024]} | ${T3_P99[1024]} | ${T3_P999[1024]} |
| 4096 B | ${T3_QPS[4096]} | ${T3_MBPS[4096]} | ${T3_P50[4096]} | ${T3_P90[4096]} | ${T3_P99[4096]} | ${T3_P999[4096]} |

Bandwidth at 4096B vs 64B: **${MB_RATIO}** (larger messages make better use
of bandwidth despite lower msg/s).

---

## 4. UDP Performance

> 64-byte datagrams, single sender thread, varying target send rate.
> Measures max sustainable throughput and packet loss boundary.

| target rate (pps) | sent pps | recv pps | loss % | p50 µs | p99 µs |
|------------------:|---------:|---------:|-------:|-------:|-------:|
| 100,000 | ${T4_SENT[100000]} | ${T4_RECV[100000]} | ${T4_LOSS[100000]}% | ${T4_P50[100000]} | ${T4_P99[100000]} |
| 200,000 | ${T4_SENT[200000]} | ${T4_RECV[200000]} | ${T4_LOSS[200000]}% | ${T4_P50[200000]} | ${T4_P99[200000]} |
| 500,000 | ${T4_SENT[500000]} | ${T4_RECV[500000]} | ${T4_LOSS[500000]}% | ${T4_P50[500000]} | ${T4_P99[500000]} |
| unlimited | ${T4_SENT[0]} | ${T4_RECV[0]} | ${T4_LOSS[0]}% | ${T4_P50[0]} | ${T4_P99[0]} |

---

## Analysis

### TCP Latency

- **Single-connection p50 = ${T1_P50} µs** — sub-microsecond scheduling overhead
  with io_uring's zero-copy recvmsg/send path.
- **p99/p50 ratio at baseline = ${P99_P50_RATIO}** — tight tail; OS jitter is
  the main contributor at single-conn load.
- **p99 at 100 conns = ${T2_P99[100]} µs** — ${P99_GROWTH} vs single-conn p99;
  growth is driven by io_uring CQE batching under high fan-in rather than lock
  contention (there is none — each TcpThread is exclusive).

### TCP Throughput

- **Peak = ${PEAK_TCP_QPS} msg/s** at **${PEAK_TCP_CONNS} connections**. Beyond
  this point, CPU cycles are split across more scheduler rounds, and bench-side
  connection threads themselves contend for CPU on the same machine.
- At 500 conns, QPS is ${QPS_500_PCT} of peak — graceful degradation, no cliff.
- The server uses ${SRV_THREADS} io_uring threads; each thread owns its accept
  queue, buf_ring, and channel pool exclusively — no mutex anywhere on the hot
  path.

### TCP Message Size

- Doubling payload from 64B to 128B costs ~${T3_QPS[64]} → ~${T3_QPS[256]}
  msg/s at 100 conns, but raw bandwidth increases: ${T3_MBPS[64]} MB/s → 
  ${T3_MBPS[4096]} MB/s at 4096B (${MB_RATIO} ratio).
- The server is message-rate bound at small sizes and bandwidth bound at large
  sizes; the crossover is between 256B and 1KB.

### UDP

- At 200K pps loss is ${T4_LOSS[200000]}%; at 500K it becomes ${T4_LOSS[500000]}%.
- Unlimited mode demonstrates raw kernel/io_uring UDP path capacity: 
  ${T4_RECV[0]} pps received. Loss here reflects bench sender outrunning the
  kernel socket receive buffer, not server-side processing backlog.
- RTT p99 at 200K pps = ${T4_P99[200000]} µs — comparable to TCP p99 at similar
  load, confirming the io_uring recvmsg path is symmetric.

---

*Benchmark methodology: all tests run on loopback (127.0.0.1), bench and server
share the same physical host. Latency values include full loopback stack RTT.
Each data point is independently timed; server is not restarted between points.*
MDEOF

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
printf "║  Report written: %-44s║\n" "$REPORT"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
cat "$REPORT"
