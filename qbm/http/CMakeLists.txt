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
# qb HTTP Module Examples - CMake Configuration
#
# This file configures comprehensive HTTP server and client examples
# demonstrating routing, middleware, authentication, and advanced features.
# -----------------------------------------------------------------------------

project(qb_http_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "HTTP module examples for the qb framework"
)

qb_status_message("Building qb HTTP module examples")

# Define HTTP examples to build
set(QB_HTTP_EXAMPLES
    01_hello_world_server
    02_simple_client
    03_basic_routing
    04_middleware_demo
    05_controller_pattern
    06_async_handlers
    07_rest_api_json
    08_static_files
    09_jwt_auth
    10_request_validation
    11_https_server
    12_http2_server
)

# Build each HTTP example
foreach(EXAMPLE_NAME ${QB_HTTP_EXAMPLES})
    qb_add_executable(
        NAME ${EXAMPLE_NAME}
        SOURCES ${EXAMPLE_NAME}.cpp
        DEPENDS qbm-http
    )
endforeach()

# Special handling for examples requiring static resources
qb_copy_resources(
    TARGET 08_static_files
    RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DESTINATION "${CMAKE_BINARY_DIR}/bin/resources"
)

qb_copy_resources(
    TARGET 12_http2_server
    RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DESTINATION "${CMAKE_BINARY_DIR}/bin/resources"
)

list(LENGTH QB_HTTP_EXAMPLES QB_HTTP_EXAMPLES_COUNT)
qb_status_message("HTTP examples configured with ${QB_HTTP_EXAMPLES_COUNT} targets")