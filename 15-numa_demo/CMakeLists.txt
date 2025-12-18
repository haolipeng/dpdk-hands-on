cmake_minimum_required(VERSION 3.10)

# NUMA demo executable
add_executable(numa_demo numa_demo.c)

# Set compile flags using target_compile_options
target_compile_options(numa_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(numa_demo PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(numa_demo ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(numa_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
