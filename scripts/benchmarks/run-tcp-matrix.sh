#!/usr/bin/env bash

run_tcp_matrix()
{
    printf '\n=== TCP connection scale ===\n'
    local required_per_worker scale_pool_size workers connections size
    required_per_worker=$(((SCALE_CONNECTIONS + SELECTED_WORKERS - 1) / SELECTED_WORKERS))
    scale_pool_size=$(next_power_of_two "$((required_per_worker * 2))")
    start_tcp_server "$SELECTED_WORKERS" "$scale_pool_size"
    "$BUILD_DIR/connection_scale" --connections "$SCALE_CONNECTIONS" \
        --hold "$SCALE_HOLD" --batch-size "$SCALE_BATCH" --payload-size 64 |
        tee "$RESULT_DIR/connection-scale.txt"
    local server_rss_kib
    server_rss_kib=$(awk '/VmRSS:/ {print $2; exit}' "/proc/$SERVER_PID/status")
    printf '  server RSS:       %s KiB\n' "${server_rss_kib:-unknown}" |
        tee -a "$RESULT_DIR/connection-scale.txt"
    record_result connection-scale "TCP connection scale"

    printf '\n=== TCP worker scaling: 64 B, 64 connections ===\n'
    for workers in $WORKER_POINTS; do
        start_tcp_server "$workers" "$TCP_POOL_SIZE"
        run_benchmark "tcp-workers-$workers" "TCP workers=$workers" \
            --no-udp --no-sweep --conns 64 --tcp-size 64 --duration "$DURATION"
    done

    printf '\n=== TCP connection scaling: 64 B, %s workers ===\n' "$SELECTED_WORKERS"
    start_tcp_server "$SELECTED_WORKERS" "$TCP_POOL_SIZE"
    for connections in $TCP_CONNECTION_POINTS; do
        (( connections <= SELECTED_WORKERS * TCP_POOL_SIZE )) || continue
        run_benchmark "tcp-conns-$connections" "TCP connections=$connections" \
            --no-udp --no-sweep --conns "$connections" --tcp-size 64 --duration "$DURATION"
    done

    printf '\n=== TCP message-size scaling: 64 connections, %s workers ===\n' "$SELECTED_WORKERS"
    for size in $TCP_SIZE_POINTS; do
        run_benchmark "tcp-size-$size" "TCP message size=$size B" \
            --no-udp --no-sweep --conns 64 --tcp-size "$size" --duration "$DURATION"
    done
}