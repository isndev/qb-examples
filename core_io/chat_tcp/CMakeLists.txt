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
# TCP Chat Example - CMake Configuration
#
# This example demonstrates a complete TCP chat system using qb actors
# for handling multiple clients, rooms, and real-time messaging.
# -----------------------------------------------------------------------------

project(qb_tcp_chat_example
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "TCP chat server/client example using qb framework"
)

qb_status_message("Building TCP chat example")

# Build components
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client) 