# QB-IO examples
# Each example demonstrates a specific aspect of the qb-io library

# Example 1: Asynchronous I/O fundamentals
add_executable(example1_async_io example1_async_io.cpp)
target_link_libraries(example1_async_io PRIVATE qb-core qb-io)

# Example 2: File I/O operations
add_executable(example2_file_io example2_file_io.cpp)
target_link_libraries(example2_file_io PRIVATE qb-core qb-io)

# Example 3: TCP networking
add_executable(example3_tcp_networking example3_tcp_networking.cpp)
target_link_libraries(example3_tcp_networking PRIVATE qb-core qb-io)

# Example 4: UDP networking
add_executable(example4_udp_networking example4_udp_networking.cpp)
target_link_libraries(example4_udp_networking PRIVATE qb-core qb-io)

# Example 5: Memory filesystem
add_executable(example5_memory_filesystem example5_memory_filesystem.cpp)
target_link_libraries(example5_memory_filesystem PRIVATE qb-core qb-io)

# Example 6: Custom Protocol
add_executable(example6_custom_protocol example6_custom_protocol.cpp)
target_link_libraries(example6_custom_protocol PRIVATE qb-core qb-io) 