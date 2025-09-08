# helloworld executable
add_executable(helloworld main.c)

# Set compile flags using target_compile_options
target_compile_options(helloworld PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(helloworld PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(helloworld ${DPDK_LINK_FLAGS})

# Set target properties
set_target_properties(helloworld PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)