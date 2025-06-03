cmake_minimum_required(VERSION 3.14)
project(qb_examples CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We're building as part of a larger project, so we can use the targets directly
# No need for find_package when we're building within the same project

# Add subdirectories
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(core_io)
add_subdirectory(qbm/redis)
add_subdirectory(qbm/pgsql)
add_subdirectory(qbm/http)
add_subdirectory(qbm/ws)