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
# Pre-3.0 holding directory — see examples/qbm/CMakeLists.txt for why this file is here.
# -----------------------------------------------------------------------------

foreach (EXAMPLE_NAME
         example2_hash_operations
         example3_list_operations
         example8_complex_actor_system)
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-redis
    )
endforeach ()
