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
# Pre-3.0 holding directory — four programs the restructure has not placed yet.
#
# The other six moved to `01-actors/` under derived target names. These four did not,
# and the reason is the same for each: the architecture retires them, and a retirement
# only lands WITH its replacement, which is not written yet. Retiring one now would
# leave the corpus promising something no file delivers — the exact defect the tier
# structure exists to make impossible.
#
#   example6_shared_queue.cpp          -> to be replaced by 01-actors/03-event-payloads,
#                                         which teaches the same foreign-thread problem with
#                                         lockfree::spsc::ringbuffer instead of a std::mutex
#                                         queue in the directory whose job is to teach that
#                                         actors do not share state
#   example7_pub_sub.cpp               -> replaced by 04-patterns/01-pubsub, which LANDED: it
#                                         uses the shipped qb::PubSub<Topic> this file names in
#                                         a comment at :64 and then does not use. The
#                                         retirement is unblocked and is a step of its own --
#                                         a deletion is never a side effect of writing the
#                                         replacement
#   example9_trading_system.cpp        -> to be replaced by 07-applications/03-market-data-hub
#   example10_distributed_computing.cpp-> the two things it hand-rolls are replaced by
#                                         04-patterns/03-worker-pool and
#                                         04-patterns/04-scatter-gather, and its monitoring
#                                         half by 02-io/11-logging-and-metrics. ALL THREE
#                                         LANDED, so this retirement is unblocked too and is
#                                         a step of its own
#
# They keep their pre-3.0 hand-written target names on purpose: a derived name would put
# them in a tier, and they are not in one. When each replacement lands, this directory
# and this file go with the last of them.
# -----------------------------------------------------------------------------

project(qb_core_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Pre-3.0 core examples awaiting their replacements"
)

qb_status_message("Building qb core examples (pre-3.0, awaiting replacement)")

qb_add_executable(
        NAME example6_shared_queue
        SOURCES example6_shared_queue.cpp
        DEPENDS qb-core
)

qb_add_executable(
        NAME example7_pub_sub
        SOURCES example7_pub_sub.cpp
        DEPENDS qb-core
)

qb_add_executable(
        NAME example9_trading_system
        SOURCES example9_trading_system.cpp
        DEPENDS qb-core
)

qb_add_executable(
        NAME example10_distributed_computing
        SOURCES example10_distributed_computing.cpp
        DEPENDS qb-core
)
