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
# Message Broker Example - CMake Configuration
#
# This example demonstrates a custom message broker system using qb actors
# for publish/subscribe messaging patterns and event distribution.
# -----------------------------------------------------------------------------

project(qb_message_broker_example
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Message broker server/client example using qb framework"
)

qb_status_message("Building message broker example")

# Build components
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client) 