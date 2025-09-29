add_executable(flow_manager main.c flow_table.c flow_table.h)

# Set compile flags using target_compile_options
target_compile_options(flow_manager PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(flow_manager PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries
target_link_libraries(flow_manager ${DPDK_LINK_FLAGS})

# Set target properties
set_target_properties(flow_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)