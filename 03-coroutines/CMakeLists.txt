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
#     03-coroutines/01-first-coroutine.cpp          ->  qb-example-coroutines-first-coroutine
#     03-coroutines/02-actor-coroutines.cpp         ->  qb-example-coroutines-actor-coroutines
#     03-coroutines/03-awaiting-oninit.cpp          ->  qb-example-coroutines-awaiting-oninit
#     03-coroutines/04-ask-request-response.cpp     ->  qb-example-coroutines-ask-request-response
#     03-coroutines/05-combinators.cpp              ->  qb-example-coroutines-combinators
#     03-coroutines/06-cancellation.cpp             ->  qb-example-coroutines-cancellation
#     03-coroutines/07-structured-concurrency.cpp   ->  qb-example-coroutines-structured-concurrency
#     03-coroutines/08-bounded-fan-out.cpp          ->  qb-example-coroutines-bounded-fan-out
#     03-coroutines/09-channels.cpp                 ->  qb-example-coroutines-channels
#     03-coroutines/10-generators.cpp               ->  qb-example-coroutines-generators
#     03-coroutines/11-async-streams.cpp            ->  qb-example-coroutines-async-streams
#     03-coroutines/12-sync-primitives.cpp          ->  qb-example-coroutines-sync-primitives
#     03-coroutines/13-retry-and-single-flight.cpp  ->  qb-example-coroutines-retry-and-single-flight
#     03-coroutines/14-foreign-awaitables.cpp       ->  qb-example-coroutines-foreign-awaitables
#
# 07..14 closed the corpus's largest coroutine gap. Before them, `coroutine_scope` and its
# three exit policies, `parallel`/`parallel_map`/`repeat_while`, `channel` + `select`,
# `generator`/`async_generator`/`async_stream`/`interval`, all six sync primitives,
# `shared_task`, the qb-io `with_retry` family and the `wait_readable`/`async_awaiter`
# adapters had ZERO demonstrators across the whole corpus. They are grouped by the problem
# each solves, not one primitive per file: 07 who owns a spawned coroutine, 08 how many run
# at once, 09 handing values between two of them, 10 producing a sequence lazily, 11
# composing one over time, 12 mutual exclusion across a co_await, 13 a flaky call several
# callers want, 14 awaiting something qb does not own.
#
# ONE OWNERSHIP NOTE THAT IS NOT OBVIOUS FROM THE FILENAMES. `04-ask-request-response`
# teaches the one-to-one exchange only. The fan-out shapes built on it (`ask_all`,
# `ask_any`, `ask_quorum`) and the shared-budget algebra (`deadline`, `deadline_in`,
# `remaining`, `ask_by`) live in `04-patterns/04-scatter-gather`, which landed first and
# is their owner page. Neither file repeats the other.
#
# 01, 05 and 07..14 DEPEND on qb-io alone -- all ten are standalone by definition and have
# neither qb::Main nor qb::Actor; the old CMakeLists linked qb-core into 01 and that made
# the claim untestable. 02, 03, 04 and 06 are actor programs and need qb-core.
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
qb_example(SOURCE 07-structured-concurrency.cpp DEPENDS qb-io)
qb_example(SOURCE 08-bounded-fan-out.cpp DEPENDS qb-io)
qb_example(SOURCE 09-channels.cpp DEPENDS qb-io)
qb_example(SOURCE 10-generators.cpp DEPENDS qb-io)
qb_example(SOURCE 11-async-streams.cpp DEPENDS qb-io)
qb_example(SOURCE 12-sync-primitives.cpp DEPENDS qb-io)
qb_example(SOURCE 13-retry-and-single-flight.cpp DEPENDS qb-io)
qb_example(SOURCE 14-foreign-awaitables.cpp DEPENDS qb-io)
