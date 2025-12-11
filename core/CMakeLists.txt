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
# qb Core Examples - CMake Configuration
#
# This file configures examples demonstrating the core actor system features
# including actor creation, message passing, lifecycle management, and patterns.
# -----------------------------------------------------------------------------

# Define project with same version as parent
project(qb_core_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Core actor system examples for the qb framework"
)

qb_status_message("Building qb core examples")

# Example 1: Simple actor example
qb_add_executable(
    NAME example1_simple_actor
    SOURCES example1_simple_actor.cpp
    DEPENDS qb-core
)

# Example 2: Basic actor communication
qb_add_executable(
    NAME example2_basic_actors
    SOURCES example2_basic_actors.cpp
    DEPENDS qb-core
)

# Example 3: Multiple cores and event priorities
qb_add_executable(
    NAME example3_multicore
    SOURCES example3_multicore.cpp
    DEPENDS qb-core
)

# Example 4: Actor lifecycle and callbacks
qb_add_executable(
    NAME example4_lifecycle
    SOURCES example4_lifecycle.cpp
    DEPENDS qb-core
)

# Example 5: Timer usage with the latest API
qb_add_executable(
    NAME example5_timers
    SOURCES example5_timers.cpp
    DEPENDS qb-core
)

# Example 6: Shared queue between actors
qb_add_executable(
    NAME example6_shared_queue
    SOURCES example6_shared_queue.cpp
    DEPENDS qb-core
)

# Example 7: Publisher/Subscriber pattern
qb_add_executable(
    NAME example7_pub_sub
    SOURCES example7_pub_sub.cpp
    DEPENDS qb-core
)

# Example 8: State machine implementation
qb_add_executable(
    NAME example8_state_machine
    SOURCES example8_state_machine.cpp
    DEPENDS qb-core
)

# Example 9: Trading system simulation
qb_add_executable(
    NAME example9_trading_system
    SOURCES example9_trading_system.cpp
    DEPENDS qb-core
)

# Example 10: Distributed computing system
qb_add_executable(
    NAME example10_distributed_computing
    SOURCES example10_distributed_computing.cpp
    DEPENDS qb-core
) 