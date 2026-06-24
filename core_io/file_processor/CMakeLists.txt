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
# File Processor Example - CMake Configuration
#
# This example demonstrates file processing using qb actors to handle
# concurrent file operations, monitoring, and batch processing.
# -----------------------------------------------------------------------------

project(qb_file_processor_example
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "File processing example using qb framework"
)

qb_status_message("Building file processor example")

# File processor executable
qb_add_executable(
        NAME file_processor
        SOURCES main.cpp
        DEPENDS qb-core
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
) 