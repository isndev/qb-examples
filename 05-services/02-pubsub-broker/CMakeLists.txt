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
# 05-services/02-pubsub-broker — a topic broker and its client
#
# Zero-copy fan-out: one message body held by a shared_ptr, N events carrying views into
# it. Same two-binary shape as 01-tcp-chat, and the same reason the executables are
# declared here rather than in server/ and client/ (see that file's header):
#
#     05-services/02-pubsub-broker + ROLE server  ->  qb-example-services-pubsub-broker-server
#     05-services/02-pubsub-broker + ROLE client  ->  qb-example-services-pubsub-broker-client
# -----------------------------------------------------------------------------

project(qb_examples_05_pubsub_broker
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Message broker server/client — zero-copy topic fan-out"
)

qb_status_message("Building qb examples — 05-services/02-pubsub-broker")

add_subdirectory(shared)

qb_example(
        SOURCE
        server/main.cpp
        server/AcceptActor.cpp
        server/ServerActor.cpp
        server/BrokerSession.cpp
        server/TopicManagerActor.cpp
        ROLE server
        DEPENDS qb-example-services-pubsub-broker-shared qb-core
)

qb_example(
        SOURCE
        client/main.cpp
        client/ClientActor.cpp
        client/InputActor.cpp
        ROLE client
        DEPENDS qb-example-services-pubsub-broker-shared qb-core
)
