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
# Message Broker Shared Library - CMake Configuration
#
# This library contains shared protocol definitions and events used by both
# the broker server and client applications.
# -----------------------------------------------------------------------------

# Shared library for broker components
qb_add_library(
        NAME broker_shared
        SOURCES
        Protocol.h
        Protocol.cpp
        Events.h
        DEPENDS qb-core
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
) 