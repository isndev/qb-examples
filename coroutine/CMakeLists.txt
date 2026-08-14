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
# qb Coroutine Examples - CMake Configuration
#
# Demonstrates the qb-io coroutine model: standalone coroutine driving with
# run_sync(), and coroutines hosted inside actors.
# -----------------------------------------------------------------------------

project(qb_coroutine_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Coroutine examples for the qb framework"
)

qb_status_message("Building qb coroutine examples")

# Standalone coroutine driving (pure qb-io, no actor framework).
qb_add_executable(
        NAME standalone_timer_example
        SOURCES standalone_timer_example.cpp
        DEPENDS qb-core qb-io
)

# Coroutines hosted inside actors.
qb_add_executable(
        NAME actor_example
        SOURCES actor_example.cpp
        DEPENDS qb-core qb-io
)
