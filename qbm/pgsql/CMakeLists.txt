cmake_minimum_required(VERSION 3.14)
project(QbPgsqlExamples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define executables for each example
set(QB_PGSQL_EXAMPLES
    example1_basic_query
    example2_prepared_statements
    example3_transaction_management
#    example4_data_types
#    example5_error_handling
)

foreach(EXAMPLE_NAME ${QB_PGSQL_EXAMPLES})
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_NAME}.cpp)
    # Link against QB components.
    # The target names QB::core, QB::io, QB::pgsql are conventional for modern CMake packages.
    # Verify these are the correct imported target names from the QB framework.
    target_link_libraries(${EXAMPLE_NAME} PRIVATE qb::core qbm-pgsql)
endforeach()

# To make it easier to run examples, you could add install rules
# or custom commands, but this is a basic build setup.

# Message to guide the user
message(STATUS "QB PostgreSQL examples configured. Build each example, e.g., 'make example1_basic_query'")
message(STATUS "Ensure your PG_CONNECTION_STRING is correctly set in each .cpp file before running!") 