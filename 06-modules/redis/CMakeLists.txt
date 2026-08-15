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
#     02-data-types.cpp                  ->  qb-example-modules-redis-data-types
#     03-coroutines-and-pipelining.cpp   ->  qb-example-modules-redis-coroutines-and-pipelining
#     04-pubsub.cpp                      ->  qb-example-modules-redis-pubsub
#     05-transactions.cpp                ->  qb-example-modules-redis-transactions
#     06-streams.cpp                     ->  qb-example-modules-redis-streams
#     07-scripting.cpp                   ->  qb-example-modules-redis-scripting
#     08-sorted-sets-and-ttl.cpp         ->  qb-example-modules-redis-sorted-sets-and-ttl
#     09-reliability.cpp                 ->  qb-example-modules-redis-reliability
#     10-cache-actor.cpp                 ->  qb-example-modules-redis-cache-actor
#
# All ten are written. 02-data-types is the MERGE of the two pre-3.0 hash/list programs plus SETS,
# which had no demonstrator anywhere; those originals have since been RETIRED, and 02-data-types
# picked up the five commands (HKEYS, HVALS, LINDEX, LSET, BLPOP) the merge had otherwise dropped.
# 07-scripting, 08-sorted-sets-and-ttl and 09-reliability are new.
#
# EVERY program here deletes the keys it wrote, on the failure path as well as the success one.
# That is not tidiness, and 06-streams is why the rule exists: it used to write ~1,000,000 stream
# entries per run and leave them behind (measured: XLEN 1,000,546 and ~240 MB from a clean key),
# and a program whose cost depends on how many times it has been run cannot be judged by a
# timeout — measured at 43 s, 74 s, 150 s and, from a freshly deleted key, 300 s without
# finishing. It now writes 40 entries to a key unique to the run and deletes it on the way out.
# -----------------------------------------------------------------------------

project(qb_examples_06_redis
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Redis module examples"
)

qb_status_message("Building qb examples — 06-modules/redis")

qb_example(SOURCE 01-connect.cpp DEPENDS qbm-redis)
qb_example(SOURCE 02-data-types.cpp DEPENDS qbm-redis)
qb_example(SOURCE 03-coroutines-and-pipelining.cpp DEPENDS qbm-redis)
qb_example(SOURCE 04-pubsub.cpp DEPENDS qbm-redis)
qb_example(SOURCE 05-transactions.cpp DEPENDS qbm-redis)
qb_example(SOURCE 06-streams.cpp DEPENDS qbm-redis)
qb_example(SOURCE 07-scripting.cpp DEPENDS qbm-redis)
qb_example(SOURCE 08-sorted-sets-and-ttl.cpp DEPENDS qbm-redis)
qb_example(SOURCE 09-reliability.cpp DEPENDS qbm-redis)
qb_example(SOURCE 10-cache-actor.cpp DEPENDS qbm-redis)
