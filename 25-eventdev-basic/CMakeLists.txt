cmake_minimum_required(VERSION 3.10)

# 查找DPDK
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# 添加可执行文件
add_executable(eventdev_demo eventdev_demo.c)

# 添加头文件目录
target_include_directories(eventdev_demo PRIVATE ${DPDK_INCLUDE_DIRS})

# 添加编译选项
target_compile_options(eventdev_demo PRIVATE -mssse3 -march=native)

# 链接DPDK库
target_link_libraries(eventdev_demo ${DPDK_LIBRARIES})

# 设置输出目录
set_target_properties(eventdev_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
