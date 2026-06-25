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
        13_coroutine_handlers
)

# Build each HTTP example
foreach (EXAMPLE_NAME ${QB_HTTP_EXAMPLES})
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-http
    )
endforeach ()

# Some examples read assets at runtime: the static site (08), the HTTP/2 site (12),
# and the self-signed dev TLS certificate under resources/ssl (11 + 12). They load
# them with a plain "resources/..." relative path; qb_stage_example_resources stages the
# resources/ tree right next to the built executables so that just works everywhere,
# keeping the example sources free of any path plumbing.
qb_stage_example_resources(
        STAGE_NAME qb_http_examples_resources
        TARGETS 08_static_files 11_https_server 12_http2_server)

list(LENGTH QB_HTTP_EXAMPLES QB_HTTP_EXAMPLES_COUNT)
qb_status_message("HTTP examples configured with ${QB_HTTP_EXAMPLES_COUNT} targets")