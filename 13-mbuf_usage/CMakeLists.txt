# mbuf_basics - Mbuf basics and data operations demo (combined lesson 2 & 3)
add_executable(mbuf_basics mbuf_basics.c)
target_compile_options(mbuf_basics PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(mbuf_basics PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(mbuf_basics ${DPDK_LINK_FLAGS})
set_target_properties(mbuf_basics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# build_udp_packet - UDP packet construction demo with pcap output
# Requires libpcap-dev: sudo apt-get install libpcap-dev
add_executable(build_udp_packet build_udp_packet.c)
target_compile_options(build_udp_packet PRIVATE ${DPDK_COMPILE_FLAGS})
target_compile_definitions(build_udp_packet PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(build_udp_packet ${DPDK_LINK_FLAGS} pcap)
set_target_properties(build_udp_packet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
