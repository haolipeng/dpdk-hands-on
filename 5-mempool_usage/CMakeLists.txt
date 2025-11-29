# mempool_usage executable (original demo)
add_executable(mempool_usage main.c)
target_compile_options(mempool_usage PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(mempool_usage PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(mempool_usage ${DPDK_LINK_FLAGS})
set_target_properties(mempool_usage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
