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
# Tier 01 — the actor model
#
# Actors, events, cores, lifetime. No socket, no protocol, no module. This tier has
# no prerequisite of its own; every later tier has it.
#
# Every target below is DERIVED from the directory and the source file by
# `qb_example` (examples/cmake/qbExample.cmake) — no target name is written here,
# and the binary carries the target's name because OUTPUT_NAME is never set:
#
#     01-actors/01-hello-actor.cpp          ->  qb-example-actors-hello-actor
#     01-actors/02-messaging.cpp            ->  qb-example-actors-messaging
#     01-actors/04-cores-and-placement.cpp  ->  qb-example-actors-cores-and-placement
#     01-actors/05-lifecycle.cpp            ->  qb-example-actors-lifecycle
#     01-actors/06-doing-things-later.cpp   ->  qb-example-actors-doing-things-later
#     01-actors/09-state-machine.cpp        ->  qb-example-actors-state-machine
#     01-actors/12-lockfree-bridge.cpp      ->  qb-example-actors-lockfree-bridge
#
# The numbering is DENSE now: this block used to record 03, 07, 08, 10 and 11 as the tier's
# to-do list and all five have since been written, so the note is kept only as the reason the
# holes were left rather than closed by renumbering — a hole costs nothing and names what is
# missing, while renumbering moves every citation into the tier on the same day.
#
# 12-lockfree-bridge is the tier's boundary case and belongs HERE rather than in 02-io: its
# subject is a thread that is NOT a VirtualCore handing work to an actor, which is a fact about
# the actor model and not about I/O. 03-event-payloads already owns the single-producer half of
# exactly that problem (`qb::lockfree::spsc::ringbuffer` + an ICallback drain); 12 is the
# multi-producer half, plus the SpinLock the mpsc ring is built out of. Tier 02 is sockets,
# timers, files, framing and TLS — a lock-free queue is none of those, and filing it there would
# be the "which library does this link" axis the tree's design explicitly rejects.
#
# DEPENDS is qb-core alone. qb-core links qb-io transitively — an actor's coroutine
# sleep is qb-io's — but naming qb-io here would say this tier teaches it, and it does
# not: tier 02 does, standalone.
# -----------------------------------------------------------------------------

project(qb_examples_01_actors
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 01 — the actor model"
)

qb_status_message("Building qb examples — tier 01-actors")

qb_example(SOURCE 01-hello-actor.cpp DEPENDS qb-core)
qb_example(SOURCE 02-messaging.cpp DEPENDS qb-core)
qb_example(SOURCE 03-event-payloads.cpp DEPENDS qb-core)
qb_example(SOURCE 04-cores-and-placement.cpp DEPENDS qb-core)
qb_example(SOURCE 05-lifecycle.cpp DEPENDS qb-core)
qb_example(SOURCE 06-doing-things-later.cpp DEPENDS qb-core)
qb_example(SOURCE 07-service-actor.cpp DEPENDS qb-core)
qb_example(SOURCE 08-child-actors.cpp DEPENDS qb-core)
qb_example(SOURCE 09-state-machine.cpp DEPENDS qb-core)
qb_example(SOURCE 10-signals-and-shutdown.cpp DEPENDS qb-core)
qb_example(SOURCE 11-hot-path.cpp DEPENDS qb-core)
qb_example(SOURCE 12-lockfree-bridge.cpp DEPENDS qb-core)
