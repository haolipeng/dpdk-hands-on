# Example 1: Basic cmdline usage
add_executable(cmdline_basic cmdline_basic.c)
target_compile_options(cmdline_basic PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(cmdline_basic PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(cmdline_basic ${DPDK_LINK_FLAGS})
set_target_properties(cmdline_basic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Example 2: Token types showcase
add_executable(cmdline_tokens cmdline_tokens.c)
target_compile_options(cmdline_tokens PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(cmdline_tokens PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(cmdline_tokens ${DPDK_LINK_FLAGS})
set_target_properties(cmdline_tokens PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Example 3: Ring manager with dynamic completion
add_executable(cmdline_ring_mgr cmdline_ring_mgr.c)
target_compile_options(cmdline_ring_mgr PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(cmdline_ring_mgr PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(cmdline_ring_mgr ${DPDK_LINK_FLAGS})
set_target_properties(cmdline_ring_mgr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Example 4: Full management console
add_executable(cmdline_console cmdline_console.c)
target_compile_options(cmdline_console PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(cmdline_console PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(cmdline_console ${DPDK_LINK_FLAGS})
set_target_properties(cmdline_console PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
