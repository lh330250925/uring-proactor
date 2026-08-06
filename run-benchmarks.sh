#!/usr/bin/env bash

set -Eeuo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$ROOT"

MODE=full
case "${1:-}" in
    "") ;;
    --quick) MODE=quick ;;
    --ci) MODE=ci ;;
    -h|--help)
        cat <<'EOF'
Usage: ./run-benchmarks.sh [--quick|--ci]

  default   Release build and comprehensive local loopback matrix
  --quick   Release build and reduced one-second matrix
  --ci      Build only; does not start benchmark servers

Environment overrides:
  BUILD_DIR, BUILD_TYPE, DURATION, SELECTED_WORKERS, WORKER_POINTS,
  TCP_CONNECTION_POINTS, TCP_SIZE_POINTS, UDP_RATE_POINTS,
  UDP_SENDER_POINTS, SCALE_CONNECTIONS, SCALE_HOLD, SCALE_BATCH,
  TCP_POOL_SIZE, REPORT, CMAKE_GENERATOR
EOF
        exit 0
        ;;
    *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
esac

source "$ROOT/scripts/benchmarks/common.sh"
source "$ROOT/scripts/benchmarks/build-project.sh"
source "$ROOT/scripts/benchmarks/run-tcp-matrix.sh"
source "$ROOT/scripts/benchmarks/run-udp-matrix.sh"
source "$ROOT/scripts/benchmarks/write-report.sh"

initialize_benchmark
trap stop_server EXIT INT TERM
configure_and_build

if [[ $MODE == ci ]]; then
    printf 'Build passed.\n'
    exit 0
fi

run_tcp_matrix
run_udp_matrix
stop_server
write_benchmark_report

printf '\nFull matrix passed. Report: %s\n' "$REPORT"