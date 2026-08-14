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
# Tier 03 — the 3.0 concurrency model
#
# Coroutines standalone (01) and hosted inside actors (02). Prerequisites are tier 01
# and 02-io/01-event-loop: a reader needs an actor to spawn from and a loop to be
# resumed by.
#
# Every target below is DERIVED from the directory and the source file by
# `qb_example` (examples/cmake/qbExample.cmake):
#
#     03-coroutines/01-first-coroutine.cpp   ->  qb-example-coroutines-first-coroutine
#     03-coroutines/02-actor-coroutines.cpp  ->  qb-example-coroutines-actor-coroutines
#
# The holes 03..11 are this tier's to-do list, and they are the corpus's largest
# measured gap after the patterns tier: `when_any`, `coro_with_timeout`,
# `coroutine_scope`, `channel`, `generator`, `async_stream`, `shared_task`, the sync
# primitives and the retry family have zero demonstrators anywhere.
#
# 01 DEPENDS qb-io alone -- it is standalone by definition and has neither qb::Main nor
# qb::Actor; the old CMakeLists linked qb-core into it and that made the claim
# untestable. 02 is an actor program and needs qb-core.
# -----------------------------------------------------------------------------

project(qb_examples_03_coroutines
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 03 — the 3.0 concurrency model"
)

qb_status_message("Building qb examples — tier 03-coroutines")

qb_example(SOURCE 01-first-coroutine.cpp DEPENDS qb-io)
qb_example(SOURCE 02-actor-coroutines.cpp DEPENDS qb-core)
