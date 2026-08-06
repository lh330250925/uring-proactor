#!/usr/bin/env bash

SERVER_PID=
declare -a RESULT_NAMES=()
declare -a RESULT_TITLES=()

die()
{
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need()
{
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

next_power_of_two()
{
    local value=$1 result=1
    while (( result < value )); do
        result=$((result * 2))
    done
    printf '%s\n' "$result"
}

normalize_points()
{
    local maximum=$1 points=$2
    tr ' ' '\n' <<< "$points" |
        awk -v maximum="$maximum" '$1 ~ /^[0-9]+$/ && $1 >= 1 && $1 <= maximum && !seen[$1]++' |
        sort -n | xargs
}

stop_server()
{
    if [[ -z ${SERVER_PID:-} ]]; then
        return
    fi
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        for _ in {1..20}; do
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.05
        done
        kill -KILL "$SERVER_PID" 2>/dev/null || true
    fi
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=
}

wait_for_listeners()
{
    local protocol=$1 port=$2 workers=$3 count
    for _ in {1..600}; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            cat "$SERVER_LOG" >&2 || true
            die "$protocol server exited before becoming ready"
        fi
        if [[ $protocol == TCP ]]; then
            count=$(ss -H -ltn "sport = :$port" 2>/dev/null | wc -l)
        else
            count=$(ss -H -lun "sport = :$port" 2>/dev/null | wc -l)
        fi
        if (( count >= workers )); then
            return
        fi
        sleep 0.05
    done
    cat "$SERVER_LOG" >&2 || true
    die "$protocol server did not create $workers listeners within 30 seconds"
}

start_tcp_server()
{
    local workers=$1 pool_size=$2
    stop_server
    printf '==> Start TCP server: workers=%s pool=%s/worker\n' "$workers" "$pool_size"
    "$BUILD_DIR/echo_server" --threads "$workers" --pool-size "$pool_size" \
        --buf-pool-size 2048 --buf-ring-size 1024 --channel-capacity 16 \
        --queue-depth 512 --no-udp >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    wait_for_listeners TCP 8080 "$workers"
}

start_udp_server()
{
    local workers=$1
    stop_server
    printf '==> Start UDP server: workers=%s\n' "$workers"
    "$BUILD_DIR/echo_server" --threads "$workers" --msghdr-pool-size 256 \
        --buf-pool-size 1024 --buf-ring-size 512 --channel-capacity 64 \
        --queue-depth 512 --no-tcp >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    wait_for_listeners UDP 8081 "$workers"
}

record_result()
{
    RESULT_NAMES+=("$1")
    RESULT_TITLES+=("$2")
}

run_benchmark()
{
    local name=$1 title=$2
    shift 2
    printf '==> %s\n' "$title"
    "$BUILD_DIR/bench" "$@" 2>&1 | tee "$RESULT_DIR/$name.txt"
    if grep -qE 'Throughput: +0 msg/s|Received: +0 pps|recv pps +0([. ]|$)' \
        "$RESULT_DIR/$name.txt"; then
        die "$title produced a zero-throughput result"
    fi
    record_result "$name" "$title"
}

initialize_benchmark()
{
    BUILD_DIR=${BUILD_DIR:-build-test}
    BUILD_TYPE=${BUILD_TYPE:-Release}
    CPU_COUNT=$(nproc)
    SELECTED_WORKERS=${SELECTED_WORKERS:-$(((CPU_COUNT + 1) / 2))}
    TCP_POOL_SIZE=${TCP_POOL_SIZE:-2048}
    SCALE_BATCH=${SCALE_BATCH:-256}
    REPORT=${REPORT:-reports/benchmark-report.md}
    SERVER_LOG="$BUILD_DIR/echo-server.log"
    RESULT_DIR="$BUILD_DIR/test-results"

    if [[ $MODE == quick ]]; then
        DURATION=${DURATION:-1}
        SCALE_CONNECTIONS=${SCALE_CONNECTIONS:-1000}
        SCALE_HOLD=${SCALE_HOLD:-1}
        WORKER_POINTS=${WORKER_POINTS:-"1 $SELECTED_WORKERS $CPU_COUNT"}
        TCP_CONNECTION_POINTS=${TCP_CONNECTION_POINTS:-"1 64 512"}
        TCP_SIZE_POINTS=${TCP_SIZE_POINTS:-"64 4096 16384"}
        UDP_RATE_POINTS=${UDP_RATE_POINTS:-"200000 500000"}
        UDP_SENDER_POINTS=${UDP_SENDER_POINTS:-"1 8"}
    else
        DURATION=${DURATION:-5}
        SCALE_CONNECTIONS=${SCALE_CONNECTIONS:-10000}
        SCALE_HOLD=${SCALE_HOLD:-2}
        WORKER_POINTS=${WORKER_POINTS:-"1 2 4 8 $SELECTED_WORKERS $CPU_COUNT"}
        TCP_CONNECTION_POINTS=${TCP_CONNECTION_POINTS:-"1 16 64 256 512"}
        TCP_SIZE_POINTS=${TCP_SIZE_POINTS:-"64 1024 4096 16384"}
        UDP_RATE_POINTS=${UDP_RATE_POINTS:-"100000 200000 400000 600000"}
        UDP_SENDER_POINTS=${UDP_SENDER_POINTS:-"1 2 4 8"}
    fi

    need cmake
    need pkg-config
    need nproc
    need ss
    pkg-config --exists liburing || die "liburing development files were not found"
    pkg-config --exists numa || die "libnuma development files were not found"

    (( SELECTED_WORKERS >= 1 && SELECTED_WORKERS <= CPU_COUNT )) ||
        die "SELECTED_WORKERS must be between 1 and $CPU_COUNT"
    (( DURATION > 0 && SCALE_CONNECTIONS > 0 && SCALE_HOLD > 0 && SCALE_BATCH > 0 )) ||
        die "duration and scale parameters must be positive"
    (( TCP_POOL_SIZE > 0 && (TCP_POOL_SIZE & (TCP_POOL_SIZE - 1)) == 0 )) ||
        die "TCP_POOL_SIZE must be a positive power of two"

    WORKER_POINTS=$(normalize_points "$CPU_COUNT" "$WORKER_POINTS")
    [[ -n $WORKER_POINTS ]] || die "WORKER_POINTS has no valid values"

    mkdir -p "$RESULT_DIR" "$(dirname -- "$REPORT")"
    rm -f "$RESULT_DIR"/*.txt
}