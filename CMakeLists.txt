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

# The example root, in one variable, because qb_example() derives a target from a
# directory's path RELATIVE to it. Set before the include: the wrapper's self-test runs
# at include time and the wrapper refuses to run at all without this.
set(QB_EXAMPLES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Shared helper: derive an example's CMake target and binary name from the tier
# directory and the source file, so the four naming layers cannot drift. See the header
# of that file for the defect class it closes.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qbExample.cmake)

# The tree is organised BY LEVEL (`NN-<tier>/`) — the order in which a person should
# read it — rather than by which library a program links, which is a fact about the
# build system and not about learning. Six tiers are converted; `core/` is what remains
# of the pre-3.0 layout, and it holds only the four programs whose replacements are not
# written yet (see its own CMakeLists for which replaces which).
#
# 04-patterns/ has no directory because it has no program: the eleven shipped
# qb/core/patterns/ headers are demonstrated zero times, and that whole tier is new work.

add_subdirectory(01-actors)
add_subdirectory(02-io)
add_subdirectory(03-coroutines)
add_subdirectory(05-services)
add_subdirectory(06-modules)
add_subdirectory(07-applications)

# Pre-3.0 holding directories. Each holds only programs the architecture RETIRES or MERGES
# rather than moves, and a retirement lands with its replacement — not before it, or the
# corpus promises something no file delivers. Each directory's own CMakeLists names which
# replacement it is waiting for; the last one to arrive takes the directory with it.
add_subdirectory(core)
add_subdirectory(core_io)
add_subdirectory(qbm)