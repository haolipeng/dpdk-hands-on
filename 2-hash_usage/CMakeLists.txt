# hash_usage executable
add_executable(hash_usage main.c)

# Set compile flags using target_compile_options
target_compile_options(hash_usage PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(hash_usage PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(hash_usage ${DPDK_LINK_FLAGS})

# Set target properties
set_target_properties(hash_usage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)