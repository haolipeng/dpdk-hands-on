# SPDX-License-Identifier: BSD-3-Clause
# DPDK Multiprocess Examples - CMake Build Configuration

cmake_minimum_required(VERSION 3.10)
project(dpdk-multiprocess-examples)

# 查找DPDK包
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${DPDK_CFLAGS} -O2 -Wall -Wextra")

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================================
# 实验1: 基础Primary-Secondary示例
# =============================================================================
#基础示例的primary主程序
add_executable(mp_basic_primary
    basic/primary.c
)
target_include_directories(mp_basic_primary PRIVATE
    ${DPDK_INCLUDE_DIRS}
    basic
)
target_link_libraries(mp_basic_primary
    ${DPDK_LIBRARIES}
)

#基础示例的secondary主程序
add_executable(mp_basic_secondary
    basic/secondary.c
)
target_include_directories(mp_basic_secondary PRIVATE
    ${DPDK_INCLUDE_DIRS}
    basic
)
target_link_libraries(mp_basic_secondary
    ${DPDK_LIBRARIES}
)

# =============================================================================
# 实验2: Ring通信示例
# =============================================================================
add_executable(mp_ring_sender
    ring_comm/sender.c
)
target_include_directories(mp_ring_sender PRIVATE
    ${DPDK_INCLUDE_DIRS}
    ring_comm
)
target_link_libraries(mp_ring_sender
    ${DPDK_LIBRARIES}
)

add_executable(mp_ring_receiver
    ring_comm/receiver.c
)
target_include_directories(mp_ring_receiver PRIVATE
    ${DPDK_INCLUDE_DIRS}
    ring_comm
)
target_link_libraries(mp_ring_receiver
    ${DPDK_LIBRARIES}
)

# =============================================================================
# 实验3: Client-Server架构
# =============================================================================
add_executable(mp_cs_server
    client_server/server.c
)
target_include_directories(mp_cs_server PRIVATE
    ${DPDK_INCLUDE_DIRS}
    client_server
)
target_link_libraries(mp_cs_server
    ${DPDK_LIBRARIES}
)

add_executable(mp_cs_client
    client_server/client.c
)
target_include_directories(mp_cs_client PRIVATE
    ${DPDK_INCLUDE_DIRS}
    client_server
)
target_link_libraries(mp_cs_client
    ${DPDK_LIBRARIES}
)

# =============================================================================
# 安装规则
# =============================================================================
install(TARGETS
    mp_basic_primary mp_basic_secondary
    mp_ring_sender mp_ring_receiver
    mp_cs_server mp_cs_client
    RUNTIME DESTINATION bin
)

# 打印构建信息
message(STATUS "==========================================")
message(STATUS "DPDK Multiprocess Examples")
message(STATUS "==========================================")
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK cflags: ${DPDK_CFLAGS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "==========================================")
message(STATUS "Targets:")
message(STATUS "  实验1: mp_basic_primary, mp_basic_secondary")
message(STATUS "  实验2: mp_ring_sender, mp_ring_receiver")
message(STATUS "  实验3: mp_cs_server, mp_cs_client")
message(STATUS "==========================================")
