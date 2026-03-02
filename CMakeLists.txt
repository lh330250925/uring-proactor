cmake_minimum_required(VERSION 3.20)
project(proactor_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED liburing)

# ---------- 公共源文件（不含 main 入口）----------
set(COMMON_SOURCES
    src/log.cpp
    src/ring.cpp
    src/timestamp.cpp
)

# ---------- 主程序 ----------
add_executable(${PROJECT_NAME}
    ${COMMON_SOURCES}
    src/main.cpp
)

# ---------- 性能基准 ----------
add_executable(proactor_bench
    ${COMMON_SOURCES}
    src/bench.cpp
)

# ---------- 公共配置宏 ----------
foreach(TARGET ${PROJECT_NAME} proactor_bench)
    target_include_directories(${TARGET} PRIVATE
        include
        ${URING_INCLUDE_DIRS}
    )
    target_link_libraries(${TARGET} PRIVATE
        ${URING_LIBRARIES}
    )
    target_compile_options(${TARGET} PRIVATE
        -O2
        -Wall
        -Wextra
        -Wno-interference-size
        ${URING_CFLAGS_OTHER}
    )
endforeach()
