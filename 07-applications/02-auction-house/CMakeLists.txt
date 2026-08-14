# ──────────────────────────────────────────────────────────────────────────────
# QB Auction House – Real-Time Bidding Example
#
# Demonstrates the full QB stack:
#   • qb-core   – Actor model (TcpListener, AuctionManager)
#   • qb-io     – Async TCP, event loop
#   • qbm-http  – HTTP/1.1 REST API, middleware
#   • qbm-pgsql – Async PostgreSQL with execute_file()
#   • qbm-redis – Redis cache + Pub/Sub
#   • qbm-http  – WebSocket (qb::http::ws) sessions + real-time broadcast
#
# Directory layout:
#   include/
#     events.h                  – inter-actor QB events
#     models/                   – domain models (Lot, Bid, User)
#     actors/                   – all actors (header-only + impl)
#   src/
#     main.cpp
#     actors/
#       auction_manager.cpp     – HTTP API + DB + cache + WS
#       websocket_handler.cpp   – WS upgrade + Redis broadcast
#   resources/
#     init_db.sql               – database schema
#     static/                   – frontend SPA files
# ──────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.22)

project(auction_house
        LANGUAGES CXX
        VERSION 1.0.0
        DESCRIPTION "QB Auction House – Real-time bidding system"
)

if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()

qb_status_message("Building QB Auction House (real-time bidding)")

# ── Sources ───────────────────────────────────────────────────────────────────

set(SOURCES
        src/main.cpp
        src/actors/auction_manager.cpp
        src/actors/websocket_handler.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────

# REQUIRES ssl: the live-bidding feed is WebSocket, and ws/ws.h #errors without
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

# Compile-time constants:
# RESOURCES_PATH – path to static files
# INIT_DB_PATH   – path to init_db.sql for database initialization
# Each example copies into its own bin/<example>/resources subdir so parallel
# builds never race (or clobber each other) on a shared bin/resources directory.
target_compile_definitions(${QB_APP_TARGET} PRIVATE
        RESOURCES_PATH="${CMAKE_BINARY_DIR}/bin/qb-example-applications-auction-house/resources/static"
        INIT_DB_PATH="${CMAKE_BINARY_DIR}/bin/qb-example-applications-auction-house/resources/init_db.sql"
)

# ── Resources ─────────────────────────────────────────────────────────────────

qb_copy_resources(
        TARGET ${QB_APP_TARGET}
        RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        DESTINATION "${CMAKE_BINARY_DIR}/bin/qb-example-applications-auction-house/resources"
)

qb_status_message("QB Auction House configured")
