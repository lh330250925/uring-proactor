cmake_minimum_required(VERSION 3.20)
project(uring_proactor CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)
pkg_check_modules(NUMA REQUIRED IMPORTED_TARGET numa)

add_library(uring_proactor STATIC
    src/buffer/buf_pool.cpp
    src/buffer/buffer.cpp
    src/buffer/msghdr_pool.cpp
    src/core/awaiter.cpp
    src/core/buf_ring.cpp
    src/core/io_ring.cpp
    src/core/io_thread.cpp
    src/core/tcp_thread.cpp
    src/core/udp_thread.cpp
    src/net/acceptor.cpp
    src/net/channel.cpp
    src/net/handler.cpp
    src/net/ip_address.cpp
    src/net/server.cpp
    src/net/socket.cpp
    src/net/tcp_channel_pool.cpp
    src/util/log.cpp
    src/util/timestamp.cpp
)

target_include_directories(uring_proactor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(uring_proactor PUBLIC
    PkgConfig::URING
    PkgConfig::NUMA
)

target_compile_options(uring_proactor PRIVATE
    -Wall -Wextra -O3
)

add_executable(echo_server benchmarks/echo_server.cpp)
target_link_libraries(echo_server PRIVATE uring_proactor)

find_package(Threads REQUIRED)

add_executable(bench benchmarks/bench.cpp)
target_link_libraries(bench PRIVATE Threads::Threads)

add_executable(connection_scale benchmarks/connection_scale.cpp)
