cmake_minimum_required(VERSION 3.14)
project(file_processor)

# Set C++ standard to use
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We're building as part of a larger project, so we can use the targets directly
# No need for find_package when we're building within the same project

# Add executable
add_executable(file_processor 
    main.cpp
)

# Include header directories
target_include_directories(file_processor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link with qb-core (which includes qb-io)
target_link_libraries(file_processor PRIVATE
    qb-core
) 