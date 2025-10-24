# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2024

cmake_minimum_required(VERSION 3.10)
project(dpdk_timer C)

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

# 定义周期性定时器可执行文件
add_executable(periodic_timer periodic_timer.c)
target_link_libraries(periodic_timer ${DPDK_LIBRARIES})

# 定义单次定时器可执行文件
add_executable(single_timer single_timer.c)
target_link_libraries(single_timer ${DPDK_LIBRARIES})

# 添加自定义目标：运行周期性定时器
add_custom_target(run-periodic
    COMMAND sudo ${CMAKE_CURRENT_BINARY_DIR}/periodic_timer -l 0-1 -n 4
    DEPENDS periodic_timer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running DPDK Periodic Timer Demo..."
)

# 添加自定义目标：运行单次定时器
add_custom_target(run-single
    COMMAND sudo ${CMAKE_CURRENT_BINARY_DIR}/single_timer -l 0-1 -n 4
    DEPENDS single_timer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running DPDK Single Timer Demo..."
)

# 打印配置信息
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK library dirs: ${DPDK_LIBRARY_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")

# 帮助信息
add_custom_target(help-cmake
    COMMAND ${CMAKE_COMMAND} -E echo "Available CMake targets:"
    COMMAND ${CMAKE_COMMAND} -E echo "  make                  - Build both timer programs"
    COMMAND ${CMAKE_COMMAND} -E echo "  make periodic_timer   - Build periodic timer only"
    COMMAND ${CMAKE_COMMAND} -E echo "  make single_timer     - Build single timer only"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-periodic     - Run the periodic timer demo"
    COMMAND ${CMAKE_COMMAND} -E echo "  make run-single       - Run the single timer demo"
    COMMAND ${CMAKE_COMMAND} -E echo "  make clean            - Remove built binaries"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Manual usage:"
    COMMAND ${CMAKE_COMMAND} -E echo "  sudo ./periodic_timer -l 0-1 -n 4"
    COMMAND ${CMAKE_COMMAND} -E echo "  sudo ./single_timer -l 0-1 -n 4"
)
