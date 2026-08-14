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
# Wire protocol and events shared by the broker server and its client.
#
# A LIBRARY, not an example: `qb_example` makes executables and derives their names, and
# this target is linked by two of them. Its name is therefore written by hand -- and is
# written to match what the two binaries derive, so `nm`, a link error or a build log
# names the project it belongs to. CMake target names are ONE FLAT NAMESPACE across the
# whole superproject; `chat_shared` and `broker_shared`, which this replaces, were unique
# by luck.
# -----------------------------------------------------------------------------

qb_add_library(
        NAME qb-example-services-pubsub-broker-shared
        SOURCES
        Protocol.h
        Protocol.cpp
        Events.h
        DEPENDS qb-core
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
)
