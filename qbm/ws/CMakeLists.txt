#
# qb - C++ Actor Framework
# Copyright (c) 2011-2025 qb - isndev (cpp.actor). All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# -----------------------------------------------------------------------------
# qb WebSocket Module Examples - CMake Configuration
#
# This file configures WebSocket server and client examples demonstrating
# real-time communication, chat systems, and WebSocket protocols.
# -----------------------------------------------------------------------------

project(qb_ws_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "WebSocket module examples for the qb framework"
)

qb_status_message("Building qb WebSocket module examples")

# Define WebSocket examples to build
set(QB_WS_EXAMPLES
        01_chat_server
        02_chat_client
)

# Build each WebSocket example.
#
# ALL of them REQUIRE ssl — not for TLS (these speak plaintext ws://) but at LINK level:
# ws/ws.h #errors with "websocket protocol requires OpenSSL crypto library" because the
# Sec-WebSocket-Accept handshake is SHA-1 + base64 from qb::io::crypto. Same crypto-link
# reasoning as the `REQUIRES ssl` on the ws unit tests in qbm/http/tests/CMakeLists.txt.
foreach (EXAMPLE_NAME ${QB_WS_EXAMPLES})
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-http
            REQUIRES ssl
    )
endforeach ()

# Stage the chat web assets next to the built executable so the example loads them with
# a plain "resources/chat/..." relative path (see qb_stage_example_resources). Skipped
# entirely when the server was gated out — the helper requires at least one live target.
if (TARGET 01_chat_server)
    qb_stage_example_resources(
            STAGE_NAME qb_ws_examples_resources
            TARGETS 01_chat_server)
endif ()

set(QB_WS_EXAMPLES_COUNT 0)
foreach (EXAMPLE_NAME ${QB_WS_EXAMPLES})
    if (TARGET ${EXAMPLE_NAME})
        math(EXPR QB_WS_EXAMPLES_COUNT "${QB_WS_EXAMPLES_COUNT} + 1")
    endif ()
endforeach ()
qb_status_message("WebSocket examples configured with ${QB_WS_EXAMPLES_COUNT} targets") 