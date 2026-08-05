#!/usr/bin/env bash

set -euo pipefail
cd "$(dirname "$0")"

BENCH=${BENCH:-./build/bench}
SERVER=${SERVER:-./build/echo_server}
REPORT=${REPORT:-./perf_report_fair.md}
DURATION=${BENCH_DURATION:-10}
REPEATS=${BENCH_REPEATS:-3}
WARMUP=${BENCH_WARMUP:-2}
SERVER_THREADS=${SERVER_THREADS:-4}
SERVER_PID=

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
cleanup() {
    if [[ -n ${SERVER_PID:-} ]]; then
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=
    fi
}
trap cleanup EXIT INT TERM

[[ -x $BENCH ]] || die "bench not found; run: cmake --build build"
[[ -x $SERVER ]] || die "echo_server not found; run: cmake --build build"
command -v taskset >/dev/null || die "taskset is required for CPU isolation"
(( REPEATS > 0 && REPEATS % 2 == 1 )) || die "BENCH_REPEATS must be a positive odd number"

if [[ -z ${SERVER_CPUS:-} || -z ${CLIENT_CPUS:-} ]]; then
    mapfile -t TOP_CPUS < <(
        lscpu -b -e=CPU,MAXMHZ,ONLINE 2>/dev/null |
            awk '$3 == "yes" { print $1, $2 }' |
            sort -k2,2nr -k1,1n |
                head -8 |
            awk '{ print $1 }'
    )
            ((${#TOP_CPUS[@]} >= 8)) || die "at least eight online CPUs are required; set SERVER_CPUS and CLIENT_CPUS explicitly"
            SERVER_CPUS=${SERVER_CPUS:-${TOP_CPUS[0]},${TOP_CPUS[2]},${TOP_CPUS[4]},${TOP_CPUS[6]}}
            CLIENT_CPUS=${CLIENT_CPUS:-${TOP_CPUS[1]},${TOP_CPUS[3]},${TOP_CPUS[5]},${TOP_CPUS[7]}}
fi

[[ $SERVER_CPUS != "$CLIENT_CPUS" ]] || die "SERVER_CPUS and CLIENT_CPUS must not be identical"

median() { printf '%s\n' "$@" | sort -n | awk '{ value[NR]=$1 } END { print value[(NR+1)/2] }'; }
range()  { printf '%s\n' "$@" | sort -n | awk 'NR == 1 { min=$1 } { max=$1 } END { print min ".." max }'; }
qps()    { grep -oE '[0-9]+ msg/s' <<< "$1" | grep -oE '^[0-9]+' | head -1; }
p50()    { grep 'p50 ' <<< "$1" | grep -oE '[0-9]+\.[0-9]+' | head -1; }
p99()    { grep 'p99 ' <<< "$1" | grep -oE '[0-9]+\.[0-9]+' | head -1; }
udp_recv() { grep 'Received:' <<< "$1" | grep -oE '[0-9]+ pps' | grep -oE '^[0-9]+' | head -1; }
udp_loss() { grep 'Packet loss' <<< "$1" | grep -oE '[0-9]+\.[0-9]+' | head -1; }
udp_scale_field() {
    grep -E "^│  $1 " <<< "$3" | tr -d '%│' | awk -v field="$2" '{print $field}'
}

start_server() {
    local protocol=$1
    pkill -9 -f "build/echo_server" 2>/dev/null || true
    taskset -c "$SERVER_CPUS" "$SERVER" --threads "$SERVER_THREADS" \
        "--no-$([[ $protocol == tcp ]] && echo udp || echo tcp)" \
        > /tmp/proactor_fair_server.log 2>&1 &
    SERVER_PID=$!
    sleep 1
    kill -0 "$SERVER_PID" 2>/dev/null || die "server failed; see /tmp/proactor_fair_server.log"
}

measure_tcp() {
    local conns=$1 size=$2 output
    local -a qps_values=() p50_values=() p99_values=()
    for ((round=1; round<=REPEATS; ++round)); do
        output=$(taskset -c "$CLIENT_CPUS" "$BENCH" --no-udp --no-sweep \
            --conns "$conns" --tcp-size "$size" --duration "$DURATION" 2>&1)
        qps_values+=("$(qps "$output")")
        p50_values+=("$(p50 "$output")")
        p99_values+=("$(p99 "$output")")
        printf '  TCP c=%s size=%s round=%d: %s msg/s, p50=%s us, p99=%s us\n' \
            "$conns" "$size" "$round" "${qps_values[-1]}" "${p50_values[-1]}" "${p99_values[-1]}" >&2
    done
    printf '%s|%s|%s|%s|%s|%s\n' \
        "$(median "${qps_values[@]}")" "$(range "${qps_values[@]}")" \
        "$(median "${p50_values[@]}")" "$(range "${p50_values[@]}")" \
        "$(median "${p99_values[@]}")" "$(range "${p99_values[@]}")"
}

measure_udp_rate() {
    local rate=$1 output
    local -a recv_values=() loss_values=() p50_values=() p99_values=()
    for ((round=1; round<=REPEATS; ++round)); do
        output=$(taskset -c "$CLIENT_CPUS" "$BENCH" --no-tcp --no-udp-sweep \
            --udp-rate "$rate" --duration "$DURATION" 2>&1)
        recv_values+=("$(udp_recv "$output")")
        loss_values+=("$(udp_loss "$output")")
        p50_values+=("$(p50 "$output")")
        p99_values+=("$(p99 "$output")")
        printf '  UDP rate=%s round=%d: recv=%s pps, loss=%s%%\n' \
            "$rate" "$round" "${recv_values[-1]}" "${loss_values[-1]}" >&2
    done
    printf '%s|%s|%s|%s|%s|%s\n' \
        "$(median "${recv_values[@]}")" "$(range "${recv_values[@]}")" \
        "$(median "${loss_values[@]}")" "$(median "${p50_values[@]}")" \
        "$(median "${p99_values[@]}")" "$(range "${p99_values[@]}")"
}

measure_udp_capacity() {
    local output
    local -a recv_values=() loss_values=()
    for ((round=1; round<=REPEATS; ++round)); do
        output=$(taskset -c "$CLIENT_CPUS" "$BENCH" --no-tcp --no-udp-rate \
            --duration "$DURATION" 2>&1)
        recv_values+=("$(udp_scale_field 4 3 "$output")")
        loss_values+=("$(udp_scale_field 4 4 "$output")")
        printf '  UDP senders=4 round=%d: recv=%s pps, loss=%s%%\n' \
            "$round" "${recv_values[-1]}" "${loss_values[-1]}" >&2
    done
    printf '%s|%s|%s|%s\n' \
        "$(median "${recv_values[@]}")" "$(range "${recv_values[@]}")" \
        "$(median "${loss_values[@]}")" "$(range "${loss_values[@]}")"
}

printf 'Fair benchmark: server CPUs=%s, client CPUs=%s, workers=%s\n' \
    "$SERVER_CPUS" "$CLIENT_CPUS" "$SERVER_THREADS"
printf 'Warmup=%ss, duration=%ss, repeats=%s (median reported)\n' "$WARMUP" "$DURATION" "$REPEATS"
CURRENT_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
if [[ $CURRENT_GOVERNOR != performance && $CURRENT_GOVERNOR != unknown ]]; then
    printf 'WARNING: CPU governor is %s; use the same governor on both systems.\n' "$CURRENT_GOVERNOR" >&2
fi

start_server tcp
printf 'Warming up...\n'
taskset -c "$CLIENT_CPUS" "$BENCH" --no-udp --no-sweep --conns 32 \
    --tcp-size 64 --duration "$WARMUP" >/dev/null 2>&1

IFS='|' read -r LAT_QPS LAT_QPS_RANGE LAT_P50 LAT_P50_RANGE LAT_P99 LAT_P99_RANGE < <(measure_tcp 1 64)
IFS='|' read -r TCP64_QPS TCP64_QPS_RANGE TCP64_P50 TCP64_P50_RANGE TCP64_P99 TCP64_P99_RANGE < <(measure_tcp 100 64)
IFS='|' read -r TCP4K_QPS TCP4K_QPS_RANGE TCP4K_P50 TCP4K_P50_RANGE TCP4K_P99 TCP4K_P99_RANGE < <(measure_tcp 100 4096)

cleanup
start_server udp
taskset -c "$CLIENT_CPUS" "$BENCH" --no-tcp --no-udp-sweep --udp-rate 100000 \
    --duration "$WARMUP" >/dev/null 2>&1
IFS='|' read -r UDP_RECV UDP_RECV_RANGE UDP_LOSS UDP_P50 UDP_P99 UDP_P99_RANGE < <(measure_udp_rate 200000)
IFS='|' read -r UDPC_RECV UDPC_RECV_RANGE UDPC_LOSS UDPC_LOSS_RANGE < <(measure_udp_capacity)

DATE=$(date +'%Y-%m-%d %H:%M')
CPU_MODEL=$(lscpu 2>/dev/null | awk -F: '/Model name/ { sub(/^[ \t]+/, "", $2); print $2; exit }')
GOVERNOR=$CURRENT_GOVERNOR
BUILD_TYPE=$(awk -F= '/CMAKE_BUILD_TYPE:/ { print $2 }' build/CMakeCache.txt 2>/dev/null || echo unknown)

cat > "$REPORT" <<EOF
# Proactor Server Fair Benchmark

Generated: ${DATE}

## Controlled Environment

| Item | Value |
|:-----|:------|
| OS | $(uname -sr) |
| CPU | ${CPU_MODEL:-unknown} |
| CPU governor | ${GOVERNOR} |
| Build type | ${BUILD_TYPE} |
| Server workers | ${SERVER_THREADS} |
| Server CPU set | ${SERVER_CPUS} |
| Client CPU set | ${CLIENT_CPUS} |
| Warmup | ${WARMUP}s |
| Sample duration | ${DURATION}s |
| Repeats | ${REPEATS}; median reported |

Server and client use disjoint CPU sets chosen from the eight highest-frequency
online CPUs. Both machines being compared must use the same worker count,
duration, repeats, message sizes, and connection counts.

## Results

| Workload | Throughput median | Throughput range | p50 median | p50 range | p99 median | p99 range |
|:---------|------------------:|:-----------------|-----------:|:----------|-----------:|:----------|
| TCP latency, 1 conn, 64 B | ${LAT_QPS} msg/s | ${LAT_QPS_RANGE} | ${LAT_P50} us | ${LAT_P50_RANGE} | ${LAT_P99} us | ${LAT_P99_RANGE} |
| TCP throughput, 100 conns, 64 B | ${TCP64_QPS} msg/s | ${TCP64_QPS_RANGE} | ${TCP64_P50} us | ${TCP64_P50_RANGE} | ${TCP64_P99} us | ${TCP64_P99_RANGE} |
| TCP large-message throughput, 100 conns, 4096 B | ${TCP4K_QPS} msg/s | ${TCP4K_QPS_RANGE} | ${TCP4K_P50} us | ${TCP4K_P50_RANGE} | ${TCP4K_P99} us | ${TCP4K_P99_RANGE} |
| UDP controlled load, 200K pps | ${UDP_RECV} pps | ${UDP_RECV_RANGE} | ${UDP_P50} us | - | ${UDP_P99} us | ${UDP_P99_RANGE} |

UDP controlled-load median loss: **${UDP_LOSS}%**.

## Capacity Test

| Workload | Receive median | Receive range | Loss median | Loss range |
|:---------|---------------:|:--------------|------------:|:-----------|
| UDP overload, 4 senders | ${UDPC_RECV} pps | ${UDPC_RECV_RANGE} | ${UDPC_LOSS}% | ${UDPC_LOSS_RANGE}% |

The overload test measures capacity only. Its latency is intentionally omitted
because latency under uncontrolled packet loss is not comparable across systems.

## Interpretation Rules

- Compare TCP 1-connection p50/p99 for the combined kernel and application latency path.
- Compare TCP 100-connection throughput only at the same fixed CPU budget.
- Compare UDP latency only in the controlled 200K pps row when loss remains below 1%.
- Do not compare maximum latency; it is dominated by unrelated host scheduling events.
- This remains a same-host loopback test. A second physical load-generator machine is
  required to isolate server-only network throughput.
EOF

printf 'Report written: %s\n' "$REPORT"