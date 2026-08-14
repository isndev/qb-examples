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
# Tier 05 — actors plus qb-io: the architecture of a real server
#
# Every entry here is a multi-file PROJECT, not a single file, because that is what the
# subject is: which actor owns the acceptor, which owns the sessions, which owns the
# logic, and on which core each of them runs.
#
#     05-services/01-tcp-chat/       server + client   qb-example-services-tcp-chat-{server,client}
#     05-services/02-pubsub-broker/  server + client   qb-example-services-pubsub-broker-{server,client}
#     05-services/03-file-pipeline/  one binary        qb-example-services-file-pipeline
#
# 04-shutdown-and-drain is not written yet: SIGTERM -> stop accepting -> drain in flight
# -> flush -> exit with an honest code has no demonstrator anywhere in the corpus.
#
# The file-based project is Unix-only, as it was before the move: it uses POSIX stat()
# and mmap() with no Windows equivalent here, so it is not created there rather than
# failing to compile.
# -----------------------------------------------------------------------------

project(qb_examples_05_services
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 05 — actors + qb-io: real servers"
)

qb_status_message("Building qb examples — tier 05-services")

add_subdirectory(01-tcp-chat)
add_subdirectory(02-pubsub-broker)

if (NOT QB_PLATFORM_WINDOWS)
    add_subdirectory(03-file-pipeline)
endif ()
