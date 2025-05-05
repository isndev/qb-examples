cmake_minimum_required(VERSION 3.14)
project(file_monitor)

# Set C++ standard to use
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We're building as part of a larger project, so we can use the targets directly
# No need for find_package when we're building within the same project

# Add executable
add_executable(file_monitor 
    main.cpp
    watcher.cpp
    processor.cpp
)

# Include header directories
target_include_directories(file_monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link with qb-core (which includes qb-io)
target_link_libraries(file_monitor PRIVATE
    qb-core
) 