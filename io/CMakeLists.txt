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
# qb I/O Examples - CMake Configuration
#
# This file configures examples demonstrating I/O operations, networking,
# file handling, and asynchronous communication features.
# -----------------------------------------------------------------------------

# Define project with same version as parent
project(qb_io_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "I/O and networking examples for the qb framework"
)

qb_status_message("Building qb I/O examples")

# Example 1: Asynchronous I/O fundamentals
qb_add_executable(
    NAME example1_async_io
    SOURCES example1_async_io.cpp
    DEPENDS qb-core qb-io
)

# Example 2: File I/O operations (Unix/Linux/macOS only)
if(NOT QB_PLATFORM_WINDOWS)
    qb_add_executable(
        NAME example2_file_io
        SOURCES example2_file_io.cpp
        DEPENDS qb-core qb-io
    )
endif()

# Example 3: TCP networking
qb_add_executable(
    NAME example3_tcp_networking
    SOURCES example3_tcp_networking.cpp
    DEPENDS qb-core qb-io
)

# Example 4: UDP networking
qb_add_executable(
    NAME example4_udp_networking
    SOURCES example4_udp_networking.cpp
    DEPENDS qb-core qb-io
)

# Example 5: Custom Protocol implementation
qb_add_executable(
    NAME example5_custom_protocol
    SOURCES example5_custom_protocol.cpp
    DEPENDS qb-core qb-io
) 