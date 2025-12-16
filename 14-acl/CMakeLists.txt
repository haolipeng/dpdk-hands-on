# acl_demo executable
add_executable(acl_demo acl_demo.c)
target_compile_options(acl_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(acl_demo PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(acl_demo ${DPDK_LINK_FLAGS})
set_target_properties(acl_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
