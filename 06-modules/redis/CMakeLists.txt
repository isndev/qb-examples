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
# 06-modules/redis — Redis with qbm-redis
#
#     01-connect.cpp                     ->  qb-example-modules-redis-connect
#     03-coroutines-and-pipelining.cpp   ->  qb-example-modules-redis-coroutines-and-pipelining
#     04-pubsub.cpp                      ->  qb-example-modules-redis-pubsub
#     05-transactions.cpp                ->  qb-example-modules-redis-transactions
#     06-streams.cpp                     ->  qb-example-modules-redis-streams
#     10-cache-actor.cpp                 ->  qb-example-modules-redis-cache-actor
#
# 02-data-types is a MERGE of two programs that have not been merged yet (they are still in
# examples/qbm/redis/); 07-scripting, 08-sorted-sets-and-ttl and 09-reliability are new.
# -----------------------------------------------------------------------------

project(qb_examples_06_redis
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Redis module examples"
)

qb_status_message("Building qb examples — 06-modules/redis")

qb_example(SOURCE 01-connect.cpp DEPENDS qbm-redis)
qb_example(SOURCE 03-coroutines-and-pipelining.cpp DEPENDS qbm-redis)
qb_example(SOURCE 04-pubsub.cpp DEPENDS qbm-redis)
qb_example(SOURCE 05-transactions.cpp DEPENDS qbm-redis)
qb_example(SOURCE 06-streams.cpp DEPENDS qbm-redis)
qb_example(SOURCE 10-cache-actor.cpp DEPENDS qbm-redis)
