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
#     03-coroutines/01-first-coroutine.cpp        ->  qb-example-coroutines-first-coroutine
#     03-coroutines/02-actor-coroutines.cpp       ->  qb-example-coroutines-actor-coroutines
#     03-coroutines/03-awaiting-oninit.cpp        ->  qb-example-coroutines-awaiting-oninit
#     03-coroutines/04-ask-request-response.cpp   ->  qb-example-coroutines-ask-request-response
#     03-coroutines/05-combinators.cpp            ->  qb-example-coroutines-combinators
#     03-coroutines/06-cancellation.cpp           ->  qb-example-coroutines-cancellation
#
# The remaining holes 07..11 are this tier's to-do list: `coroutine_scope` +
# `parallel_map`, `channel` + `select`, `generator` / `async_generator` /
# `async_stream`, the sync primitives (`semaphore`, `async_mutex`, `barrier`,
# `shared_task`) and the retry family still have zero demonstrators anywhere.
#
# ONE OWNERSHIP NOTE THAT IS NOT OBVIOUS FROM THE FILENAMES. `04-ask-request-response`
# teaches the one-to-one exchange only. The fan-out shapes built on it (`ask_all`,
# `ask_any`, `ask_quorum`) and the shared-budget algebra (`deadline`, `deadline_in`,
# `remaining`, `ask_by`) live in `04-patterns/04-scatter-gather`, which landed first and
# is their owner page. Neither file repeats the other.
#
# 01 and 05 DEPEND on qb-io alone -- both are standalone by definition and have neither
# qb::Main nor qb::Actor; the old CMakeLists linked qb-core into 01 and that made the
# claim untestable. 02, 03, 04 and 06 are actor programs and need qb-core.
# -----------------------------------------------------------------------------

project(qb_examples_03_coroutines
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 03 — the 3.0 concurrency model"
)

qb_status_message("Building qb examples — tier 03-coroutines")

qb_example(SOURCE 01-first-coroutine.cpp DEPENDS qb-io)
qb_example(SOURCE 02-actor-coroutines.cpp DEPENDS qb-core)
qb_example(SOURCE 03-awaiting-oninit.cpp DEPENDS qb-core)
qb_example(SOURCE 04-ask-request-response.cpp DEPENDS qb-core)
qb_example(SOURCE 05-combinators.cpp DEPENDS qb-io)
qb_example(SOURCE 06-cancellation.cpp DEPENDS qb-core)
