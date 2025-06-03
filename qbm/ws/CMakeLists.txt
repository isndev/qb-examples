cmake_minimum_required(VERSION 3.14)
project(QbWsExamples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define executables for each example
set(QB_WS_EXAMPLES
    01_chat_server
    02_chat_client
)

foreach(EXAMPLE_NAME ${QB_WS_EXAMPLES})
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_NAME}.cpp)
    # Link against QB components
    target_link_libraries(${EXAMPLE_NAME} PRIVATE qb::core qbm-http qbm-websocket)
endforeach()

# Special handling for 01_chat_server - copy static resources for chat
# Define source and destination paths for chat resources
set(CHAT_RESOURCES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(CHAT_RESOURCES_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")

# Create custom command to copy chat resources during build
add_custom_command(
    TARGET 01_chat_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHAT_RESOURCES_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${CHAT_RESOURCES_SOURCE_DIR}" 
            "${CHAT_RESOURCES_DEST_DIR}"
    COMMENT "Copying chat static resources for 01_chat_server example"
    VERBATIM
)

# Also copy resources during configuration for immediate availability
if(EXISTS "${CHAT_RESOURCES_SOURCE_DIR}")
    file(COPY "${CHAT_RESOURCES_SOURCE_DIR}/" DESTINATION "${CHAT_RESOURCES_DEST_DIR}")
    message(STATUS "Copied chat resources to: ${CHAT_RESOURCES_DEST_DIR}")
else()
    message(WARNING "Chat resources directory not found: ${CHAT_RESOURCES_SOURCE_DIR}")
endif()

# Set working directory hint for the executable that uses static files
set_target_properties(01_chat_server PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# Message to guide the user
message(STATUS "QB WebSocket examples configured.")
message(STATUS "For 01_chat_server: chat resources will be available in build/examples/qbm/ws/resources/chat/")
message(STATUS "Build with: 'make 01_chat_server' or 'cmake --build . --target 01_chat_server'")
message(STATUS "Run the chat server and open http://localhost:8080 in your browser")
message(STATUS "")
message(STATUS "For 02_chat_client: Build with 'make 02_chat_client' or 'cmake --build . --target 02_chat_client'")
message(STATUS "Run with: './02_chat_client [ws://localhost:8080/ws]' to connect to the chat server")
message(STATUS "Use /help command for available chat client commands") 