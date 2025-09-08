# 3-capture_packet executable
add_executable(capture_packet main.c)

# Set compile flags using target_compile_options
target_compile_options(capture_packet PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(capture_packet PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(capture_packet ${DPDK_LINK_FLAGS})

# Set target properties
set_target_properties(capture_packet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)