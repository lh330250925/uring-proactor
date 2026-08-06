#!/usr/bin/env bash

configure_and_build()
{
    printf '==> Configure (%s)\n' "$BUILD_TYPE"
    local configure=(cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
    if [[ -n ${CMAKE_GENERATOR:-} ]]; then
        configure+=(-G "$CMAKE_GENERATOR")
    fi
    "${configure[@]}"

    printf '==> Build\n'
    cmake --build "$BUILD_DIR" --parallel "$CPU_COUNT"
}