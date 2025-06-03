cmake_minimum_required(VERSION 3.14)
project(qb_core_io_examples CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add subdirectories
if (NOT WIN32)
add_subdirectory(file_processor)
add_subdirectory(file_monitor)
endif()
add_subdirectory(chat_tcp)
add_subdirectory(message_broker)