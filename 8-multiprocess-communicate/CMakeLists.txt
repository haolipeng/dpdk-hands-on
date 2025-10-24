# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2024

cmake_minimum_required(VERSION 3.10)
project(dpdk_mp_ipc C)

# 设置C标准
set(CMAKE_C_STANDARD 11)

# 添加编译选项
add_compile_options(-O3 -g)
add_compile_definitions(ALLOW_EXPERIMENTAL_API)

# 查找DPDK包
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# 包含DPDK头文件目录
include_directories(${DPDK_INCLUDE_DIRS})

# 添加链接目录
link_directories(${DPDK_LIBRARY_DIRS})

# 定义Server可执行文件
add_executable(mp_server server.c common.h)
target_link_libraries(mp_server ${DPDK_LIBRARIES})

# 定义Client可执行文件
add_executable(mp_client client.c common.h)
target_link_libraries(mp_client ${DPDK_LIBRARIES})

# 添加自定义目标：运行Server
add_custom_target(run-server
    COMMAND sudo ${CMAKE_CURRENT_BINARY_DIR}/mp_server -l 0-1 -n 4 --proc-type=primary --file-prefix=mp_ipc
    DEPENDS mp_server
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Starting DPDK Multi-Process IPC Server..."
)

# 添加自定义目标：运行Client
add_custom_target(run-client
    COMMAND sudo ${CMAKE_CURRENT_BINARY_DIR}/mp_client -l 2-3 -n 4 --proc-type=secondary --file-prefix=mp_ipc
    DEPENDS mp_client
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Starting DPDK Multi-Process IPC Client..."
)

# 打印配置信息
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK library dirs: ${DPDK_LIBRARY_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")

# 帮助信息
add_custom_target(help-cmake
    COMMAND ${CMAKE_COMMAND} -E echo "Available CMake targets:"
    COMMAND ${CMAKE_COMMAND} -E echo "  make               - Build both server and client"
    COMMAND ${CMAKE_COMMAND} -E echo "  make mp_server     - Build server only"
    COMMAND ${CMAKE_COMMAND} -E echo "  make mp_client     - Build client only"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-server    - Run the server process"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-client    - Run the client process"
    COMMAND ${CMAKE_COMMAND} -E echo "  make clean         - Remove built binaries"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Manual usage:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Terminal 1: make run-server"
    COMMAND ${CMAKE_COMMAND} -E echo "  Terminal 2: make run-client"
)
