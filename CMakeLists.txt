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
# qb Framework Examples - CMake Configuration
#
# This file configures all examples for the qb framework, demonstrating
# various features and usage patterns across core, I/O, and modules.
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.22)
project(qb_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Examples demonstrating qb framework capabilities"
)

# Build examples only if enabled
if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()

qb_status_message("Building qb framework examples")

# Shared helper: stage an example's resources/ tree next to its built binaries so the
# example C++ loads assets with a plain "resources/..." relative path (no baked paths,
# no resolver code). Defined here so every example subdirectory can use it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qbExampleResources.cmake)

# NOTE: examples are being ported to the current qb API one at a time. Only the
# adapted ones are enabled below; uncomment each category/example as it is ported.

# Add subdirectories for each example category
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(core_io)
add_subdirectory(coroutine)

# Add module examples
add_subdirectory(qbm/redis)
add_subdirectory(qbm/pgsql)
add_subdirectory(qbm/http)
add_subdirectory(qbm/ws)

# Full-stack examples (HTTP + WS + DB + Redis)
add_subdirectory(all)