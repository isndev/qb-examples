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
# Tier 06 — the qbm modules: http · ws · pgsql · redis
#
# A tier whose second level is a GROUP rather than a project: four module surfaces,
# each holding one program per file. The group name is part of the derived target, so
# two modules may use the same slug and still name different binaries —
# `06-modules/pgsql/03-transactions.cpp` and `06-modules/redis/05-transactions.cpp`
# derive `qb-example-modules-pgsql-transactions` and
# `qb-example-modules-redis-transactions`.
#
# THE SEAM EACH GROUP'S 01 STATES, because it is the same in all three modules and is
# written down nowhere else: no qbm header includes anything under qb/core -- the string
# `qb::Actor` occurs zero times in all three module sources -- so the modules are pure
# qb-io libraries and the seam is qb-side. There are exactly two shapes of it. HTTP and
# WS: `qb::http::use<Self>::io_handler<Session>` inherited alongside `qb::Actor`.
# pgsql and redis: the client as an ordinary member, connected inside a coroutine
# `onInit`, which is why the actor is Activating and its events are stashed until the
# backend answers.
# -----------------------------------------------------------------------------

project(qb_examples_06_modules
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 06 — the qbm modules"
)

qb_status_message("Building qb examples — tier 06-modules")

add_subdirectory(http)
add_subdirectory(ws)
add_subdirectory(pgsql)
add_subdirectory(redis)
