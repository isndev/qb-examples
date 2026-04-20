# ──────────────────────────────────────────────────────────────────────────────
# QB TaskManager – Expert Architecture Example
#
# Demonstrates the full QB stack in a production-grade project structure:
#   • qb-core   – Actor model (TcpListener, TaskManager)
#   • qb-io     – Async TCP, event loop
#   • qbm-http  – HTTP/1.1 routing, middleware (CORS, logging, static files)
#   • qbm-pgsql – Async PostgreSQL with prepared statements
#   • qbm-redis – Redis cache (GET/SET/DEL) + Pub/Sub (PUBLISH/SUBSCRIBE)
#   • qbm-ws    – WebSocket sessions + real-time event broadcast
#
# Directory layout:
#   include/
#     events.h                  – inter-actor QB events
#     models/task.h             – domain models (Task, TaskList, TaskEvent)
#     actors/
#       tcp_listener.h          – TCP acceptor (header-only, trivial)
#       http_session.h          – HTTP session type
#       ws_session.h            – WebSocket session type
#       websocket_handler.h     – WS pool + Redis subscriber
#       task_manager.h          – main actor
#   src/
#     main.cpp
#     actors/
#       websocket_handler.cpp
#       task_manager.cpp
# ──────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.22)

project(taskmanager
    LANGUAGES    CXX
    VERSION      1.0.0
    DESCRIPTION  "QB TaskManager – Expert architecture example"
)

if(NOT QB_BUILD_EXAMPLES)
    return()
endif()

qb_status_message("Building QB TaskManager (expert architecture)")

# ── Sources ───────────────────────────────────────────────────────────────────

set(SOURCES
    src/main.cpp
    src/actors/task_manager.cpp
    src/actors/websocket_handler.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────

qb_add_executable(
    NAME    taskmanager
    SOURCES ${SOURCES}
    DEPENDS
        qbm-pgsql
        qbm-redis
        qbm-websocket
)

target_include_directories(taskmanager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compile-time constant: absolute path to the static files directory.
# The binary falls back to relative paths at runtime if this path is absent.
target_compile_definitions(taskmanager PRIVATE
    RESOURCES_PATH="${CMAKE_BINARY_DIR}/bin/resources/static"
)

# ── Resources ─────────────────────────────────────────────────────────────────

qb_copy_resources(
    TARGET      taskmanager
    RESOURCES   "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DESTINATION "${CMAKE_BINARY_DIR}/bin/resources"
)

qb_status_message("QB TaskManager configured")
