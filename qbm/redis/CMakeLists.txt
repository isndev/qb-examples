cmake_minimum_required(VERSION 3.14)
project(QbRedisExamples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define executables for each example
set(QB_REDIS_EXAMPLES
    example1_basic_connection
    example2_hash_operations
    example3_list_operations
    example4_async_operations
    example5_pubsub_example
    example6_transaction_example
    example7_stream_processor
    example8_complex_actor_system
)

foreach(EXAMPLE_NAME ${QB_REDIS_EXAMPLES})
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_NAME}.cpp)
    # Link against QB components.
    # The target names QB::core, QB::io, QB::pgsql are conventional for modern CMake packages.
    # Verify these are the correct imported target names from the QB framework.
    target_link_libraries(${EXAMPLE_NAME} PRIVATE qbm-redis)
endforeach()

# To make it easier to run examples, you could add install rules
# or custom commands, but this is a basic build setup.

# Message to guide the user
message(STATUS "QB Redis examples configured. Build each example, e.g., 'make example1_basic_connection'")
message(STATUS "Ensure your REDIS_CONNECTION_STRING is correctly set in each .cpp file before running!") 