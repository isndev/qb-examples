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
#
# BOTH REQUIRE ssl — not for TLS (these speak plaintext ws://) but at LINK level:
# ws/ws.h #errors with "websocket protocol requires OpenSSL crypto library" because the
# Sec-WebSocket-Accept handshake is SHA-1 + base64 from qb::io::crypto. Same crypto-link
# reasoning as the REQUIRES ssl on the ws unit tests in qbm/http/tests/CMakeLists.txt.
#
# 03-coro-session and 04-coro-client are not written yet: ws::coro_session and
# ws::coro_client have no demonstrator, and 02 hand-rolls the state machine coro_client
# replaces. When 03 is written, ITS FIXTURES MUST LIVE IN A NAMED NAMESPACE
# (qb/scripts/check-coro-fixture-linkage.py).
# -----------------------------------------------------------------------------

project(qb_examples_06_ws
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "WebSocket module examples"
)

qb_status_message("Building qb examples — 06-modules/ws")

qb_example(SOURCE 01-chat-server.cpp DEPENDS qbm-http REQUIRES ssl TARGET_VAR _tgt_ws_server)
qb_example(SOURCE 02-chat-client.cpp DEPENDS qbm-http REQUIRES ssl)

# Stage the chat web assets next to the built executable so the example loads them with a
# plain "resources/chat/..." relative path. Skipped entirely when the server was gated out —
# the helper requires at least one live target.
if (DEFINED _tgt_ws_server)
    qb_stage_example_resources(
            STAGE_NAME qb_ws_examples_resources
            TARGETS ${_tgt_ws_server})
endif ()
