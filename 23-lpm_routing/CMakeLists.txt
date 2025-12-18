cmake_minimum_required(VERSION 3.10)

# LPM routing demo executable
add_executable(lpm_demo lpm_demo.c)

# Set compile flags using target_compile_options
target_compile_options(lpm_demo PRIVATE ${DPDK_COMPILE_FLAGS} -pthread)
target_compile_definitions(lpm_demo PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries and pthread
target_link_libraries(lpm_demo ${DPDK_LINK_FLAGS} pthread)

# Set output directory to bin/
set_target_properties(lpm_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
