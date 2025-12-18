cmake_minimum_required(VERSION 3.10)

# RSS multiqueue executable
add_executable(rss_multiqueue rss_multiqueue.c)

# Set compile flags using target_compile_options
target_compile_options(rss_multiqueue PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(rss_multiqueue PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(rss_multiqueue ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(rss_multiqueue PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
