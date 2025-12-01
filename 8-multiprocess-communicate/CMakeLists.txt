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

# 设置输出目录到统一的 bin 目录
set_target_properties(mp_server mp_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# 添加自定义目标：运行Server
add_custom_target(run-server
    COMMAND sudo ${CMAKE_SOURCE_DIR}/bin/mp_server -l 0-1 -n 4 --proc-type=primary --file-prefix=mp_ipc
    DEPENDS mp_server
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    COMMENT "Starting DPDK Multi-Process IPC Server..."
)

# 添加自定义目标：运行Client
add_custom_target(run-client
    COMMAND sudo ${CMAKE_SOURCE_DIR}/bin/mp_client -l 2-3 -n 4 --proc-type=secondary --file-prefix=mp_ipc
    DEPENDS mp_client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    COMMENT "Starting DPDK Multi-Process IPC Client..."
)

# 打印配置信息
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK library dirs: ${DPDK_LIBRARY_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")
message(STATUS "Output directory: ${CMAKE_SOURCE_DIR}/bin")
