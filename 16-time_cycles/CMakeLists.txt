cmake_minimum_required(VERSION 3.10)

# Time demo executable
add_executable(time_demo time_demo.c)

# Set compile flags using target_compile_options
target_compile_options(time_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(time_demo PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(time_demo ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(time_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Benchmark executable
add_executable(benchmark benchmark.c)

# Set compile flags using target_compile_options
target_compile_options(benchmark PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(benchmark PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(benchmark ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
