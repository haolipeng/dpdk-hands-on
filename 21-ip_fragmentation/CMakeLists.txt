cmake_minimum_required(VERSION 3.10)

# IP fragmentation demo executable
add_executable(ip_frag_demo ip_frag_demo.c)

# Set compile flags using target_compile_options
target_compile_options(ip_frag_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(ip_frag_demo PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(ip_frag_demo ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(ip_frag_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
