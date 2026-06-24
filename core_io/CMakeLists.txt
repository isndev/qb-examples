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
# qb Core + I/O Integration Examples - CMake Configuration
#
# This file configures examples demonstrating integration between the core
# actor system and I/O operations for real-world applications.
# -----------------------------------------------------------------------------

# Define project with same version as parent
project(qb_core_io_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Core+I/O integration examples for the qb framework"
)

qb_status_message("Building qb core+I/O integration examples")

# File-based examples (Unix/Linux/macOS only)
if (NOT QB_PLATFORM_WINDOWS)
    add_subdirectory(file_processor)
    add_subdirectory(file_monitor)
endif ()

# Network-based examples (all platforms)
add_subdirectory(chat_tcp)
add_subdirectory(message_broker)