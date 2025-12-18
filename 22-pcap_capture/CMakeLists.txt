cmake_minimum_required(VERSION 3.10)

# PCAP capture executable
add_executable(pcap_capture pcap_capture.c)

# Set compile flags using target_compile_options
target_compile_options(pcap_capture PRIVATE ${DPDK_COMPILE_FLAGS} -pthread)
target_compile_definitions(pcap_capture PRIVATE ALLOW_EXPERIMENTAL_API)

# Link with DPDK libraries and pthread
target_link_libraries(pcap_capture ${DPDK_LINK_FLAGS} pthread)

# Set output directory to bin/
set_target_properties(pcap_capture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
