# ──────────────────────────────────────────────────────────────────────────────
# market-data-hub — the tier-7 application with NO HTTP and NO SQL.
#
# It links qb-core and qb-io and nothing else, which is the point: the other two
# applications in this tier are HTTP + PostgreSQL + Redis services, and a corpus whose
# every large program is a web service says qb is a web framework.
#
# Directory layout:
#   include/market_data/
#     model.h        – the domain, and the cross-core payload rule it obeys
#     protocol.h     – the binary wire format (size_as_header + big-endian fields)
#   src/
#     actors.h       – ingest / aggregate / publish / subscribe
#     main.cpp       – engine wiring, the foreign feed thread, the report
#
# One binary, so no ROLE: the slug comes from this directory and the target is
# `qb-example-applications-market-data-hub`.
# ──────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.22)

project(market_data_hub
        LANGUAGES CXX
        VERSION 1.0.0
        DESCRIPTION "qb market-data hub – a low-latency pipeline with no HTTP and no SQL"
)

if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()

qb_status_message("Building qb market-data-hub (low-latency pipeline)")

qb_example(
        SOURCE src/main.cpp
        DEPENDS qb-core
        INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        TARGET_VAR QB_APP_TARGET
)

if (QB_BUILD_TESTS AND TARGET ${QB_APP_TARGET})
    qb_add_test(MODULE qb-examples TIER system NAME market-feed-stop
                SOURCES tests/feed-stop.cpp DEPENDS qb-core
                INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
                LABELS serial)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME qb-examples-test-system-market-data-hub-shutdown
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_startup_failure.py $<TARGET_FILE:${QB_APP_TARGET}>)
    set_tests_properties(qb-examples-test-system-market-data-hub-shutdown
                         PROPERTIES LABELS "tier:system;module:qb-examples;network" RUN_SERIAL TRUE TIMEOUT 20)
endif ()
