#!/usr/bin/env bash

run_udp_matrix()
{
    local workers rate senders

    printf '\n=== UDP worker scaling: 64 B, 4 unlimited senders ===\n'
    for workers in $WORKER_POINTS; do
        start_udp_server "$workers"
        run_benchmark "udp-workers-$workers" "UDP workers=$workers" \
            --no-tcp --no-udp-rate --udp-size 64 --udp-senders 4 --duration "$DURATION"
    done

    printf '\n=== UDP controlled-rate curve: %s workers ===\n' "$SELECTED_WORKERS"
    start_udp_server "$SELECTED_WORKERS"
    for rate in $UDP_RATE_POINTS; do
        run_benchmark "udp-rate-$rate" "UDP target rate=$rate pps" \
            --no-tcp --no-udp-sweep --udp-size 64 --udp-rate "$rate" --duration "$DURATION"
    done

    printf '\n=== UDP sender scaling: 64 B unlimited, %s workers ===\n' "$SELECTED_WORKERS"
    for senders in $UDP_SENDER_POINTS; do
        run_benchmark "udp-senders-$senders" "UDP senders=$senders" \
            --no-tcp --no-udp-rate --udp-size 64 --udp-senders "$senders" --duration "$DURATION"
    done
}