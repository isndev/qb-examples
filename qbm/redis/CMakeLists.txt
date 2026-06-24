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
# qb Redis Module Examples - CMake Configuration
#
# This file configures Redis client examples demonstrating data structures,
# pub/sub messaging, transactions, and stream processing.
# -----------------------------------------------------------------------------

project(qb_redis_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Redis module examples for the qb framework"
)

qb_status_message("Building qb Redis module examples")

# Define Redis examples to build
set(QB_REDIS_EXAMPLES
    example1_basic_connection
    example2_hash_operations
    example3_list_operations
    example4_async_operations
    example5_pubsub_example
    example6_transaction_example
    example7_stream_processor
    example8_complex_actor_system
    coro_example
)

# Build each Redis example
foreach(EXAMPLE_NAME ${QB_REDIS_EXAMPLES})
    qb_add_executable(
        NAME ${EXAMPLE_NAME}
        SOURCES ${EXAMPLE_NAME}.cpp
        DEPENDS qbm-redis
    )
endforeach()

list(LENGTH QB_REDIS_EXAMPLES QB_REDIS_EXAMPLES_COUNT)
qb_status_message("Redis examples configured with ${QB_REDIS_EXAMPLES_COUNT} targets") 