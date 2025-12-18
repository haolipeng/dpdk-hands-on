cmake_minimum_required(VERSION 3.10)

# rte_flow demo executable
add_executable(rte_flow_demo rte_flow_demo.c)

# Set compile flags using target_compile_options
target_compile_options(rte_flow_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(rte_flow_demo PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(rte_flow_demo ${DPDK_LINK_FLAGS})

# Set output directory to bin/
set_target_properties(rte_flow_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
