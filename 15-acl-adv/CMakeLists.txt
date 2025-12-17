# acl_adv executable
add_executable(acl_adv acl_demo.c)
target_compile_options(acl_adv PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(acl_adv PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(acl_adv ${DPDK_LINK_FLAGS})
set_target_properties(acl_adv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
