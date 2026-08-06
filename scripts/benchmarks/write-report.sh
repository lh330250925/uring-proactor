#!/usr/bin/env bash

write_benchmark_report()
{
    local cpu kernel compiler governor profile report_date index name title
    cpu=$(lscpu 2>/dev/null | awk -F: '/Model name/ {sub(/^[ \t]+/, "", $2); print $2; exit}' || true)
    kernel=$(uname -sr)
    compiler=$(${CXX:-c++} --version | head -n1)
    governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || printf unknown)
    profile=$(cat /sys/firmware/acpi/platform_profile 2>/dev/null || printf unknown)
    report_date=$(date --iso-8601=seconds)

    {
        printf '# uring-proactor Local Loopback Benchmark\n\n'
        printf '| Item | Value |\n|:--|:--|\n'
        printf '| Date | %s |\n' "$report_date"
        printf '| CPU | %s |\n' "${cpu:-unknown}"
        printf '| Logical CPUs | %s |\n' "$CPU_COUNT"
        printf '| Kernel | %s |\n' "$kernel"
        printf '| Compiler | %s |\n' "$compiler"
        printf '| Build | %s |\n' "$BUILD_TYPE"
        printf '| Governor / platform profile | %s / %s |\n' "$governor" "$profile"
        printf '| Selected workers | %s |\n' "$SELECTED_WORKERS"
        printf '| Worker points | %s |\n' "$WORKER_POINTS"
        printf '| Duration | %ss per point |\n\n' "$DURATION"
        printf '## Methodology\n\n'
        printf 'Client and server run on the same host without CPU affinity, so results include '
        printf 'client work, server work, scheduling, and the Linux loopback stack. TCP and UDP '
        printf 'servers are measured separately. TCP MB/s is aggregate request plus echo traffic. '
        printf 'Unlimited UDP is an overload test; use the controlled-rate curve for sustainable '
        printf 'throughput and loss.\n\n'
        for ((index=0; index<${#RESULT_NAMES[@]}; ++index)); do
            name=${RESULT_NAMES[index]}
            title=${RESULT_TITLES[index]}
            printf '## %s\n\n```text\n' "$title"
            cat "$RESULT_DIR/$name.txt"
            printf '```\n\n'
        done
    } > "$REPORT"
}