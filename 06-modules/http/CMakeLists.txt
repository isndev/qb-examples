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
# 06-modules/http — HTTP/1.1, HTTP/2 and HTTP/3 with qbm-http
#
# Targets are DERIVED from this group's name and each source file by `qb_example`
# (examples/cmake/qbExample.cmake); no target name is written here:
#
#     01-hello-server.cpp        ->  qb-example-modules-http-hello-server
#     02-routing.cpp             ->  qb-example-modules-http-routing
#     03-controllers.cpp         ->  qb-example-modules-http-controllers
#     04-middleware.cpp          ->  qb-example-modules-http-middleware
#     05-rest-api-json.cpp       ->  qb-example-modules-http-rest-api-json
#     06-validation.cpp          ->  qb-example-modules-http-validation
#     07-auth-jwt.cpp            ->  qb-example-modules-http-auth-jwt
#     08-static-files.cpp        ->  qb-example-modules-http-static-files
#     09-coroutine-handlers.cpp  ->  qb-example-modules-http-coroutine-handlers
#     10-client.cpp              ->  qb-example-modules-http-client
#     11-https.cpp               ->  qb-example-modules-http-https
#     12-http2.cpp               ->  qb-example-modules-http-http2
#     13-http3.cpp               ->  qb-example-modules-http-http3
#     14-streaming-and-cookies.cpp -> qb-example-modules-http-streaming-and-cookies
#     15-http2-and-http3-clients.cpp -> qb-example-modules-http-http2-and-http3-clients
#
# 15 closes the one gap this group had: qb::http2::Client and qb::http3::Client were documented and
# had ZERO occurrences anywhere in the corpus — 12 and 13 are pure SERVER programs, and the only
# client examples (10, 14) are HTTP/1.1. It is gated `REQUIRES ssl` for the same reason 12 is (h2 is
# ALPN-only, there is no h2c path), and its HTTP/3 third guards itself with #ifdef
# QBM_HTTP_HAS_HTTP3 exactly as 13 does, because HTTP/3 also needs nghttp3 and the REQUIRES
# vocabulary cannot express that.
#
# Examples that cannot even COMPILE without OpenSSL are gated out of an SSL-off build
# instead of breaking it (REQUIRES — same vocabulary and same resolver as the test suite's):
#   07-auth-jwt  — <qbm/http/auth/manager.h>; auth.h #errors "requires OpenSSL crypto library"
#   11-https     — qb::http::ssl::Server<> / qb::http::ssl::DefaultSecureSession
#   12-http2     — h2 is TLS+ALPN only: qb::io::transport::stcp, no h2c path
# 13-http3 is deliberately NOT gated: it guards itself with #ifdef QBM_HTTP_HAS_HTTP3 and
# degrades to a message-only stub. That is the right shape for it, because HTTP/3 also
# needs nghttp3 — a condition the REQUIRES vocabulary (ssl/quic/compression) cannot
# express, so a gate here would be wrong wherever ngtcp2 is present but nghttp3 is not.
#
# 14-streaming-and-cookies is NOT gated on `compression` even though it demonstrates
# Body::compress: that section is guarded with #ifdef QB_HAS_COMPRESSION and states which
# branch it took, so the other five subjects (chunked framing, cookies, Form, multipart BUILD
# and the HTTP date helpers) stay available in a build without zlib. A REQUIRES here would
# remove all six.
# -----------------------------------------------------------------------------

project(qb_examples_06_http
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "HTTP module examples"
)

qb_status_message("Building qb examples — 06-modules/http")

qb_example(SOURCE 01-hello-server.cpp DEPENDS qbm-http)
qb_example(SOURCE 02-routing.cpp DEPENDS qbm-http)
qb_example(SOURCE 03-controllers.cpp DEPENDS qbm-http)
qb_example(SOURCE 04-middleware.cpp DEPENDS qbm-http)
qb_example(SOURCE 05-rest-api-json.cpp DEPENDS qbm-http)
qb_example(SOURCE 06-validation.cpp DEPENDS qbm-http)
qb_example(SOURCE 07-auth-jwt.cpp DEPENDS qbm-http REQUIRES ssl)
qb_example(SOURCE 08-static-files.cpp DEPENDS qbm-http TARGET_VAR _tgt_static)
qb_example(SOURCE 09-coroutine-handlers.cpp DEPENDS qbm-http)
qb_example(SOURCE 10-client.cpp DEPENDS qbm-http)
qb_example(SOURCE 11-https.cpp DEPENDS qbm-http REQUIRES ssl TARGET_VAR _tgt_https)
qb_example(SOURCE 12-http2.cpp DEPENDS qbm-http REQUIRES ssl TARGET_VAR _tgt_http2)
qb_example(SOURCE 13-http3.cpp DEPENDS qbm-http TARGET_VAR _tgt_http3)
qb_example(SOURCE 14-streaming-and-cookies.cpp DEPENDS qbm-http)
qb_example(SOURCE 15-http2-and-http3-clients.cpp DEPENDS qbm-http REQUIRES ssl TARGET_VAR _tgt_h2h3_clients)

# Four examples read assets at runtime: the static site (08), the HTTP/2 site (12), and the
# self-signed dev TLS certificate under resources/ssl (11, 12, and the TLS-only HTTP/3
# server 13). They load them with a plain "resources/..." relative path;
# qb_stage_example_resources stages the resources/ tree right next to the built executables
# so that just works everywhere, keeping the sources free of path plumbing.
#
# The names come back from TARGET_VAR rather than being retyped: a gated-out example leaves
# its variable UNSET, so this list holds only targets that were really created — which is
# also what keeps the helper's FATAL_ERROR on a genuinely unknown name worth having.
set(_qb_http_resource_targets)
foreach (_v _tgt_static _tgt_https _tgt_http2 _tgt_http3 _tgt_h2h3_clients)
    if (DEFINED ${_v})
        list(APPEND _qb_http_resource_targets "${${_v}}")
    endif ()
endforeach ()
qb_stage_example_resources(
        STAGE_NAME qb_http_examples_resources
        TARGETS ${_qb_http_resource_targets})

list(LENGTH _qb_http_resource_targets _n)
qb_status_message("HTTP examples: ${_n} target(s) staged with resources")
