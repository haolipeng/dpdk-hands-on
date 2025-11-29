# mbuf_hello - Mbuf basics demo
add_executable(mbuf_hello mbuf_hello.c)
target_compile_options(mbuf_hello PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(mbuf_hello PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(mbuf_hello ${DPDK_LINK_FLAGS})
set_target_properties(mbuf_hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# mbuf_data_demo - Mbuf data operations demo
add_executable(mbuf_data_demo mbuf_data_demo.c)
target_compile_options(mbuf_data_demo PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(mbuf_data_demo PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(mbuf_data_demo ${DPDK_LINK_FLAGS})
set_target_properties(mbuf_data_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# build_udp_packet - UDP packet construction demo
add_executable(build_udp_packet build_udp_packet.c)
target_compile_options(build_udp_packet PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(build_udp_packet PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(build_udp_packet ${DPDK_LINK_FLAGS})
set_target_properties(build_udp_packet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
