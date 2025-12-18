cmake_minimum_required(VERSION 3.10)

# Statistics monitor executable
add_executable(stats_monitor stats_monitor.c)

# Set compile flags using target_compile_options
target_compile_options(stats_monitor PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(stats_monitor PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(stats_monitor ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(stats_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
