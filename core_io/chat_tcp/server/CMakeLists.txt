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
# TCP Chat Server - CMake Configuration
#
# This executable implements a TCP chat server using qb actors to handle
# multiple concurrent client connections and chat rooms.
# -----------------------------------------------------------------------------

# Chat server executable
qb_add_executable(
    NAME chat_server
    SOURCES
        main.cpp
        AcceptActor.cpp
        ServerActor.cpp
        ChatSession.cpp
        ChatRoomActor.cpp
    DEPENDS
        chat_shared
        qb-core
) 