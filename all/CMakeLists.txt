# Full-stack QB examples (HTTP + WS + DB + Redis)

cmake_minimum_required(VERSION 3.16)
project(qb_examples_all LANGUAGES CXX)

if(NOT QB_BUILD_EXAMPLES)
    return()
endif()

qb_status_message("Building full-stack QB examples")

add_subdirectory(taskmanager)
add_subdirectory(auction_house)
