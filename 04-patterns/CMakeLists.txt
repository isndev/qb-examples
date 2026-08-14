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
# Tier 04 — the eleven shipped interaction patterns
#
# `qb/core/patterns/` ships eleven headers, every one unit- and benchmark-tested, and until
# this tier landed the corpus included NONE of them: zero `#include` of anything under that
# directory across 113 files, and `qb::ask` — the primitive six of the eleven are built on —
# had zero call sites in 55 programs. This tier is that gap, closed.
#
# Every target below is DERIVED from the directory and the source file by `qb_example`
# (examples/cmake/qbExample.cmake):
#
#     04-patterns/01-pubsub.cpp                    ->  qb-example-patterns-pubsub
#     04-patterns/02-supervisor.cpp                ->  qb-example-patterns-supervisor
#     04-patterns/03-worker-pool.cpp               ->  qb-example-patterns-worker-pool
#     04-patterns/04-scatter-gather.cpp            ->  qb-example-patterns-scatter-gather
#     04-patterns/05-resilience.cpp                ->  qb-example-patterns-resilience
#     04-patterns/06-streaming.cpp                 ->  qb-example-patterns-streaming
#     04-patterns/07-saga.cpp                      ->  qb-example-patterns-saga
#     04-patterns/08-batching-and-idempotency.cpp  ->  qb-example-patterns-batching-and-idempotency
#     04-patterns/09-discovery.cpp                 ->  qb-example-patterns-discovery
#
# THE NUMBERING IS DENSE HERE, unlike tiers 01 and 03, because this tier is complete: all
# eleven headers have a demonstrator. There is no hole to reserve.
#
# DEPENDS is qb-core alone, for every program. The patterns are header-only helpers layered on
# `qb::Actor` / `qb::ScopedCoroContext`, so nothing here links a library qb-core does not
# already carry — and naming qb-io would say this tier teaches the loop, which tier 02 does.
# -----------------------------------------------------------------------------

project(qb_examples_04_patterns
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 04 — the eleven shipped interaction patterns"
)

qb_status_message("Building qb examples — tier 04-patterns")

qb_example(SOURCE 01-pubsub.cpp DEPENDS qb-core)
qb_example(SOURCE 02-supervisor.cpp DEPENDS qb-core)
qb_example(SOURCE 03-worker-pool.cpp DEPENDS qb-core)
qb_example(SOURCE 04-scatter-gather.cpp DEPENDS qb-core)
qb_example(SOURCE 05-resilience.cpp DEPENDS qb-core)
qb_example(SOURCE 06-streaming.cpp DEPENDS qb-core)
qb_example(SOURCE 07-saga.cpp DEPENDS qb-core)
qb_example(SOURCE 08-batching-and-idempotency.cpp DEPENDS qb-core)
qb_example(SOURCE 09-discovery.cpp DEPENDS qb-core)
