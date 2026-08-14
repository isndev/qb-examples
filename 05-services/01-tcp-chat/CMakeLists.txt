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
# 05-services/01-tcp-chat — a TCP chat server and its client
#
# The acceptor, the per-connection session pool and the room logic each live on their own
# core; the client is a second binary that talks to it. Sources stay in server/, client/
# and shared/ -- a project's file names are free, because the target name is derived from
# the PROJECT DIRECTORY and the ROLE, not from a source file:
#
#     05-services/01-tcp-chat + ROLE server  ->  qb-example-services-tcp-chat-server
#     05-services/01-tcp-chat + ROLE client  ->  qb-example-services-tcp-chat-client
#
# WHY server/ AND client/ NO LONGER HAVE THEIR OWN CMakeLists. `qb_example` derives a
# target from a path relative to examples/ and accepts exactly two shapes -- `tier/` and
# `tier/project/`. A call from `05-services/01-tcp-chat/server` is three levels down and
# is a configure-time FATAL_ERROR by design: at that depth the derivation has no project
# directory to take a slug from, and `server` is a role, not a program. So the two
# executables are declared here, from the project directory that names them. The sources
# did not move; only the two files that used to type `NAME chat_server` did.
#
# shared/ keeps its own CMakeLists: it builds a LIBRARY, which `qb_example` does not make
# and whose depth nothing constrains.
# -----------------------------------------------------------------------------

project(qb_examples_05_tcp_chat
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "TCP chat server/client — actors over qb-io"
)

qb_status_message("Building qb examples — 05-services/01-tcp-chat")

add_subdirectory(shared)

qb_example(
        SOURCE
        server/main.cpp
        server/AcceptActor.cpp
        server/ServerActor.cpp
        server/ChatSession.cpp
        server/ChatRoomActor.cpp
        ROLE server
        DEPENDS qb-example-services-tcp-chat-shared qb-core
)

qb_example(
        SOURCE
        client/main.cpp
        client/ClientActor.cpp
        client/InputActor.cpp
        ROLE client
        DEPENDS qb-example-services-tcp-chat-shared qb-core
)
