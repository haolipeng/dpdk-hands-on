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
