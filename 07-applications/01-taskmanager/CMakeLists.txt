# ──────────────────────────────────────────────────────────────────────────────
# QB TaskManager – Expert Architecture Example
#
# Demonstrates the full QB stack in a production-grade project structure:
#   • qb-core   – Actor model (TcpListener, TaskManager)
#   • qb-io     – Async TCP, event loop
#   • qbm-http  – HTTP/1.1 routing, middleware (CORS, logging, static files)
#   • qbm-pgsql – Async PostgreSQL with prepared statements
#   • qbm-redis – Redis cache (GET/SET/DEL) + Pub/Sub (PUBLISH/SUBSCRIBE)
#   • qbm-http  – WebSocket (qb::http::ws) sessions + real-time event broadcast
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
        LANGUAGES CXX
        VERSION 1.0.0
        DESCRIPTION "QB TaskManager – Expert architecture example"
)

if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()

qb_status_message("Building QB TaskManager (expert architecture)")

# ── Sources ───────────────────────────────────────────────────────────────────

set(SOURCES
        src/main.cpp
        src/actors/task_manager.cpp
        src/actors/websocket_handler.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────

# REQUIRES ssl: the live task feed is WebSocket, and ws/ws.h #errors without
# qb::io::crypto (SHA-1 + base64 for Sec-WebSocket-Accept). Not a TLS requirement —
# a crypto-link one. Without it the target is not created at all.
qb_example(
        SOURCE ${SOURCES}
        DEPENDS
        qbm-http
        qbm-pgsql
        qbm-redis
        REQUIRES ssl
        TARGET_VAR QB_APP_TARGET
)

# The derived name, once, in a variable — so the six places below that used to retype
# it cannot come to disagree with it. A REQUIRES gate that was not met leaves
# TARGET_VAR unset, which is the same test as the if(NOT TARGET) it replaces.

# Gated out (QB_HAS_SSL=OFF): nothing below has a target to attach to.
if (NOT DEFINED QB_APP_TARGET)
    return()
endif ()

target_include_directories(${QB_APP_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compile-time constant: absolute path to the static files directory.
# The binary falls back to relative paths at runtime if this path is absent.
# Each example copies into its own bin/<example>/resources subdir so parallel
# builds never race (or clobber each other) on a shared bin/resources directory.
target_compile_definitions(${QB_APP_TARGET} PRIVATE
        RESOURCES_PATH="${CMAKE_BINARY_DIR}/bin/qb-example-applications-taskmanager/resources/static"
)

# ── Resources ─────────────────────────────────────────────────────────────────

qb_copy_resources(
        TARGET ${QB_APP_TARGET}
        RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        DESTINATION "${CMAKE_BINARY_DIR}/bin/qb-example-applications-taskmanager/resources"
)

qb_status_message("QB TaskManager configured")
