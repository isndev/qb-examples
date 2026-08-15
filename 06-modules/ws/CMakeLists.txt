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
# 06-modules/ws — WebSocket over qbm-http
#
#     01-chat-server.cpp  ->  qb-example-modules-ws-chat-server
#     02-chat-client.cpp  ->  qb-example-modules-ws-chat-client
#     03-coro-session.cpp ->  qb-example-modules-ws-coro-session
#     04-coro-client.cpp  ->  qb-example-modules-ws-coro-client
#
# BOTH REQUIRE ssl — not for TLS (these speak plaintext ws://) but at LINK level:
# ws/ws.h #errors with "websocket protocol requires OpenSSL crypto library" because the
# Sec-WebSocket-Accept handshake is SHA-1 + base64 from qb::io::crypto. Same crypto-link
# reasoning as the REQUIRES ssl on the ws unit tests in qbm/http/tests/CMakeLists.txt.
#
# 03 and 04 are the coroutine pair. BOTH declare their session type in a NAMED namespace, which
# `coro_session` requires: it spawns a lambda into a frame type defined in a qb header, so an
# anonymous-namespace session gives that frame a field of no-linkage type
# (-Werror=subobject-linkage on g++-14). qb/scripts/check-coro-fixture-linkage.py is the guard for
# that rule, but its scope today is the four TEST trees and not examples/ — these two obey it by
# hand, and extending the guard's spec list to examples/ would close the gap.
# -----------------------------------------------------------------------------

project(qb_examples_06_ws
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "WebSocket module examples"
)

qb_status_message("Building qb examples — 06-modules/ws")

qb_example(SOURCE 01-chat-server.cpp DEPENDS qbm-http REQUIRES ssl TARGET_VAR _tgt_ws_server)
qb_example(SOURCE 02-chat-client.cpp DEPENDS qbm-http REQUIRES ssl)
qb_example(SOURCE 03-coro-session.cpp DEPENDS qbm-http REQUIRES ssl)
qb_example(SOURCE 04-coro-client.cpp DEPENDS qbm-http REQUIRES ssl)

# Stage the chat web assets next to the built executable so the example loads them with a
# plain "resources/chat/..." relative path. Skipped entirely when the server was gated out —
# the helper requires at least one live target.
if (DEFINED _tgt_ws_server)
    qb_stage_example_resources(
            STAGE_NAME qb_ws_examples_resources
            TARGETS ${_tgt_ws_server})
endif ()
