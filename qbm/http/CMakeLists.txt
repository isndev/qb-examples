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
        14_http3_server
)

# Examples that cannot even COMPILE without OpenSSL, and are therefore gated out of an
# SSL-off build instead of breaking it (qb_add_executable REQUIRES — same vocabulary and
# same resolver as the test suite's):
#   09_jwt_auth    — <http/auth/manager.h>; auth.h #errors "requires OpenSSL crypto library"
#   11_https_server— qb::http::ssl::Server<> / qb::http::ssl::DefaultSecureSession
#   12_http2_server— h2 is TLS+ALPN only: qb::io::transport::stcp, no h2c path
# 14_http3_server is deliberately NOT listed: it guards itself with #ifdef
# QBM_HTTP_HAS_HTTP3 and degrades to a message-only stub. That is the right shape for it,
# because HTTP/3 also needs nghttp3 — a condition the REQUIRES vocabulary (ssl/quic/
# compression) cannot express, so a gate here would be wrong wherever ngtcp2 is present
# but nghttp3 is not.
set(QB_HTTP_EXAMPLES_SSL_ONLY 09_jwt_auth 11_https_server 12_http2_server)

# Build each HTTP example
foreach (EXAMPLE_NAME ${QB_HTTP_EXAMPLES})
    set(_requires)
    if (EXAMPLE_NAME IN_LIST QB_HTTP_EXAMPLES_SSL_ONLY)
        set(_requires REQUIRES ssl)
    endif ()
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-http
            ${_requires}
    )
endforeach ()

# Some examples read assets at runtime: the static site (08), the HTTP/2 site (12),
# and the self-signed dev TLS certificate under resources/ssl (11, 12, and the
# TLS-only HTTP/3 server 14). They load them with a plain "resources/..." relative
# path; qb_stage_example_resources stages the resources/ tree right next to the built
# executables so that just works everywhere, keeping the sources free of path plumbing.
# Filtered by if(TARGET ...) because the SSL-gated ones above do not exist in an SSL-off
# build; the helper keeps its FATAL_ERROR on a genuinely unknown name.
set(QB_HTTP_EXAMPLES_WITH_RESOURCES)
foreach (_res_tgt 08_static_files 11_https_server 12_http2_server 14_http3_server)
    if (TARGET ${_res_tgt})
        list(APPEND QB_HTTP_EXAMPLES_WITH_RESOURCES ${_res_tgt})
    endif ()
endforeach ()
qb_stage_example_resources(
        STAGE_NAME qb_http_examples_resources
        TARGETS ${QB_HTTP_EXAMPLES_WITH_RESOURCES})

# Count what was actually created, not what was listed — the SSL-gated ones are absent
# in an SSL-off build and a headline number that ignored that would hide the gate.
set(QB_HTTP_EXAMPLES_COUNT 0)
foreach (EXAMPLE_NAME ${QB_HTTP_EXAMPLES})
    if (TARGET ${EXAMPLE_NAME})
        math(EXPR QB_HTTP_EXAMPLES_COUNT "${QB_HTTP_EXAMPLES_COUNT} + 1")
    endif ()
endforeach ()
qb_status_message("HTTP examples configured with ${QB_HTTP_EXAMPLES_COUNT} targets")