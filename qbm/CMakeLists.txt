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
# Pre-3.0 holding directory — module examples the restructure has not placed yet.
#
# Everything else moved to `06-modules/<module>/` under derived target names. These four
# did not, and each for a stated reason:
#
#   http/06_async_handlers.cpp          RETIRED. Its subject is response timing and it
#                                       reports 0 ms for a 1.2 s handler, because next()
#                                       returns at the first suspension. Replaced by the
#                                       SHIPPED timing middleware in 06-modules/http/
#                                       04-middleware and by 09-coroutine-handlers.
#   redis/example2_hash_operations.cpp  MERGED, not moved: the two become one
#   redis/example3_list_operations.cpp  06-modules/redis/02-data-types covering strings,
#                                       hashes, lists AND sets (sets have no demonstrator
#                                       today). A merge is a new program, not a rename.
#   redis/example8_complex_actor_system RETIRED (1278 lines, never terminates, five dead
#                                       notification paths, a plain global ActorId raced
#                                       across three cores). Replaced by 06-modules/redis/
#                                       07-scripting, 10-cache-actor and 04-patterns/*.
#
# They keep their pre-3.0 hand-written target names on purpose: a derived name would put
# them in a tier, and they are not in one.
# -----------------------------------------------------------------------------

project(qb_examples_qbm_holding
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Pre-3.0 module examples awaiting merge or replacement"
)

add_subdirectory(http)
add_subdirectory(redis)
