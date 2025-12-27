cmake_minimum_required(VERSION 3.10)
project(dpdk_hands_on)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================================
# 查找外部依赖库
# ============================================================================
# 查找pkg-config
find_package(PkgConfig REQUIRED)
if(NOT PKG_CONFIG_FOUND)
    message(FATAL_ERROR "pkg-config not found. Please install pkg-config first:
    Ubuntu/Debian: sudo apt-get install pkg-config
    CentOS/RHEL: sudo yum install pkgconfig
    Fedora: sudo dnf install pkgconfig")
endif()

# 使用pkg-config查找dpdk
pkg_check_modules(DPDK REQUIRED libdpdk)
if(NOT DPDK_FOUND)
    message(FATAL_ERROR "DPDK library not found. Please install DPDK development package:
    
    If you have installed DPDK manually, please ensure:
    1. DPDK is properly installed
    2. pkg-config can find DPDK (check with: pkg-config --list-all | grep dpdk)
    3. PKG_CONFIG_PATH includes the path to DPDK's .pc file")
endif()

# 添加DPDK库目录到链接路径
link_directories(${DPDK_LIBRARY_DIRS})

# 添加DPDK头文件目录
include_directories(${DPDK_INCLUDE_DIRS})

# 设置全局编译选项
# 将DPDK标志存储为变量，供子目录使用
set(DPDK_COMPILE_FLAGS ${DPDK_CFLAGS})
set(DPDK_LINK_FLAGS ${DPDK_LIBRARIES})

# 添加子目录
add_subdirectory(1-helloworld)
add_subdirectory(2-hash_usage)
add_subdirectory(3-capture_packet)
add_subdirectory(4-parse_packet)
add_subdirectory(5-mempool_usage)
add_subdirectory(6-flow_manager)
add_subdirectory(7-multiprocess)
add_subdirectory(8-multiprocess-communicate)
add_subdirectory(9-timer)
add_subdirectory(10-sp-sc-ring)
add_subdirectory(11-mp-mc-ring)
add_subdirectory(12-hts-ring)
add_subdirectory(13-mbuf_usage)
add_subdirectory(14-acl-basic)
add_subdirectory(15-numa_demo)
add_subdirectory(15-acl-adv)
add_subdirectory(16-time_cycles)
add_subdirectory(17-cmdline)
add_subdirectory(18-rss_multiqueue)
add_subdirectory(19-rte_flow)
add_subdirectory(20-statistics)
add_subdirectory(21-ip_fragmentation)
add_subdirectory(22-pcap_capture)
add_subdirectory(23-lpm_routing)
add_subdirectory(24-graph-basic)
add_subdirectory(25-eventdev-basic)

# 打印配置信息
message(STATUS "DPDK version: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK library dirs: ${DPDK_LIBRARY_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")
