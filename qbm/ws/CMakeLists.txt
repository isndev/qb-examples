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

# Build each WebSocket example
foreach (EXAMPLE_NAME ${QB_WS_EXAMPLES})
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-http
    )
endforeach ()

# Special handling for chat server - copy static resources
qb_copy_resources(
        TARGET 01_chat_server
        RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        DESTINATION "${CMAKE_BINARY_DIR}/bin/resources"
)

list(LENGTH QB_WS_EXAMPLES QB_WS_EXAMPLES_COUNT)
qb_status_message("WebSocket examples configured with ${QB_WS_EXAMPLES_COUNT} targets") 