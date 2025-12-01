# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2024

cmake_minimum_required(VERSION 3.10)
project(dpdk_hts_ring C)

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

# 定义 HTS Ring 示例可执行文件
add_executable(hts_ring hts_ring.c)
target_link_libraries(hts_ring ${DPDK_LIBRARIES})

# 设置输出目录到统一的 bin 目录
set_target_properties(hts_ring PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# 打印配置信息
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK library dirs: ${DPDK_LIBRARY_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")
message(STATUS "Output directory: ${CMAKE_SOURCE_DIR}/bin")
