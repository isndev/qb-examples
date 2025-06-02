cmake_minimum_required(VERSION 3.14)
project(QbHttpExamples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define executables for each example
set(QB_HTTP_EXAMPLES
    01_hello_world_server
    02_simple_client
    03_basic_routing
    04_middleware_demo
    05_controller_pattern
    06_async_handlers
    07_rest_api_json
    08_static_files
    09_jwt_auth
    10_request_validation
    11_https_server
    12_http2_server
)

foreach(EXAMPLE_NAME ${QB_HTTP_EXAMPLES})
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_NAME}.cpp)
    # Link against QB components.
    # The target names QB::core, QB::io, QB::pgsql are conventional for modern CMake packages.
    # Verify these are the correct imported target names from the QB framework.
    target_link_libraries(${EXAMPLE_NAME} PRIVATE qb::core qbm-http)
endforeach()

# Special handling for 08_static_files - copy static resources
# Define source and destination paths for static resources
set(RESOURCES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(RESOURCES_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")

# Create custom command to copy resources during build for 08_static_files
add_custom_command(
    TARGET 08_static_files POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${RESOURCES_SOURCE_DIR}" 
            "${RESOURCES_DEST_DIR}"
    COMMENT "Copying static resources for 08_static_files example"
    VERBATIM
)

# Special handling for 12_http2_server - copy HTTP/2 specific resources
add_custom_command(
    TARGET 12_http2_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${RESOURCES_SOURCE_DIR}" 
            "${RESOURCES_DEST_DIR}"
    COMMENT "Copying static resources for 12_http2_server example"
    VERBATIM
)

# Also copy resources during configuration for immediate availability
if(EXISTS "${RESOURCES_SOURCE_DIR}")
    file(COPY "${RESOURCES_SOURCE_DIR}/" DESTINATION "${RESOURCES_DEST_DIR}")
    message(STATUS "Copied static resources to: ${RESOURCES_DEST_DIR}")
else()
    message(WARNING "Static resources directory not found: ${RESOURCES_SOURCE_DIR}")
endif()

# Set working directory hint for the executables that use static files
set_target_properties(08_static_files PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

set_target_properties(12_http2_server PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# To make it easier to run examples, you could add install rules
# or custom commands, but this is a basic build setup.

# Message to guide the user
message(STATUS "QB HTTP examples configured. Build each example, e.g., 'make 01_hello_world_server'")
message(STATUS "For 08_static_files: static resources will be available in build/examples/qbm/http/resources/")
message(STATUS "For 12_http2_server: HTTP/2 resources will be available in build/examples/qbm/http/resources/http2/")