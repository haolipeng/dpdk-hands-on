cmake_minimum_required(VERSION 3.10)

# 查找DPDK
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# 添加可执行文件
add_executable(graph_demo graph_demo.c)

# 添加头文件目录
target_include_directories(graph_demo PRIVATE ${DPDK_INCLUDE_DIRS})

# 添加编译选项（支持 SSSE3 等 SIMD 指令集）
target_compile_options(graph_demo PRIVATE -mssse3 -march=native)

# 链接DPDK库
target_link_libraries(graph_demo ${DPDK_LIBRARIES})

# 设置输出目录
set_target_properties(graph_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
