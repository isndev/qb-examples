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
# qb Framework Examples - CMake Configuration
#
# This file configures all examples for the qb framework, demonstrating
# various features and usage patterns across core, I/O, and modules.
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.22)
project(qb_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Examples demonstrating qb framework capabilities"
)

# Build examples only if enabled
if (NOT QB_BUILD_EXAMPLES)
    return()
endif ()

qb_status_message("Building qb framework examples")

# Shared helper: stage an example's resources/ tree next to its built binaries so the
# example C++ loads assets with a plain "resources/..." relative path (no baked paths,
# no resolver code). Defined here so every example subdirectory can use it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qbExampleResources.cmake)

# The example root, in one variable, because qb_example() derives a target from a
# directory's path RELATIVE to it. Set before the include: the wrapper's self-test runs
# at include time and the wrapper refuses to run at all without this.
set(QB_EXAMPLES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Shared helper: derive an example's CMake target and binary name from the tier
# directory and the source file, so the four naming layers cannot drift. See the header
# of that file for the defect class it closes.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qbExample.cmake)

# The tree is organised BY LEVEL (`NN-<tier>/`) — the order in which a person should
# read it — rather than by which library a program links, which is a fact about the
# build system and not about learning. Every tier is now converted: the five pre-3.0
# holding directories (`core/`, `core_io/`, `qbm/`, `coroutine/`, `all/`) are gone, and
# with them the last hand-written example target names. Every example in this tree now
# derives its target and binary from its path — there is no second convention left.
#
# 04-patterns/ is the one tier that is entirely NEW rather than moved: the eleven shipped
# qb/core/patterns/ headers had zero demonstrators anywhere in the corpus, and `qb::ask` —
# the primitive six of them are built on — had zero call sites in 55 programs.
#
# WHAT THE RETIREMENTS COST, AND WHERE IT WENT. A retirement only lands once its replacement
# does, or the corpus promises something no file delivers. Three lessons had no home when the
# holding directories were checked one program at a time, and each was given one rather than
# used as a reason to keep a superseded file alive:
#   * HKEYS/HVALS/LINDEX/LSET/BLPOP survived only in prose describing the two merged redis
#     programs -> added to 06-modules/redis/02-data-types, which IS the merge.
#   * a plain cursor-based XREAD (no consumer group) -> added to 06-modules/redis/06-streams.
#   * "relocatable is not owned": a boxed shared_ptr payload makes the EVENT memcpy-safe and
#     says nothing about who may write through the POINTEE -> added to
#     01-actors/03-event-payloads, next to the rule it is the second half of.

add_subdirectory(01-actors)
add_subdirectory(02-io)
add_subdirectory(03-coroutines)
add_subdirectory(04-patterns)
add_subdirectory(05-services)
add_subdirectory(06-modules)
add_subdirectory(07-applications)

# -----------------------------------------------------------------------------
# The roster: which examples this build produced, where each one's SOURCE is, and where its
# BINARY landed. Written by the build because the build is the only thing that knows all three.
#
# `dev/agent/run-examples.py` reads it. Everything it would otherwise have to GUESS is here
# instead, and each guess it replaces has a failure mode this tree has already paid for:
#
#   * binary path — re-deriving `qb-example-modules-http-https` from `06-modules/http/11-https.cpp`
#     means reimplementing `_qb_example_derive` in Python, which is a second copy of a convention
#     free to disagree with the first. `$<TARGET_FILE:...>` is the generator's own answer.
#   * source path — the runner reads the `@expect` lines out of the source, and a wrong
#     mapping would assert one program's output against another program's promise.
#   * gated out vs. missing — an SSL-off build does not FAIL to create the nine `REQUIRES ssl`
#     examples, it silently does not create them. A runner that only saw the survivors would
#     report nine programs as absent, or (worse) not report them at all. The `gated` records
#     make that a stated SKIP with the capability named. (Nine, measured: io 07/10, http 07/11/12,
#     ws 01/02, and both tier-7 applications — 02-io/10 also declares `compression`, so it is the
#     one entry whose `requires` column carries two tokens. 13-http3 is deliberately NOT gated —
#     it guards itself with #ifdef QBM_HTTP_HAS_HTTP3.)
#
# `file(GENERATE)` and not `file(WRITE)`: only the former expands `$<TARGET_FILE:>`, and only
# the former re-runs when the target moves. It writes at GENERATE time, which is also why this
# block is last — the global properties are complete only after every add_subdirectory above.
# -----------------------------------------------------------------------------
get_property(_qb_ex_targets GLOBAL PROPERTY QB_EXAMPLE_TARGETS)
get_property(_qb_ex_sources GLOBAL PROPERTY QB_EXAMPLE_SOURCES)
get_property(_qb_ex_gated GLOBAL PROPERTY QB_EXAMPLE_GATED)

list(LENGTH _qb_ex_targets _qb_ex_n)
list(LENGTH _qb_ex_sources _qb_ex_n_src)
if (NOT _qb_ex_n EQUAL _qb_ex_n_src)
    # The two lists are appended in the same `if (TARGET)` block, so they can only fall out of
    # step if that block is edited wrongly — and the roster would then pair each target with
    # somebody else's source, which is worse than having no roster at all.
    message(FATAL_ERROR
            "qb examples roster: ${_qb_ex_n} target(s) but ${_qb_ex_n_src} source(s) — the two "
            "GLOBAL properties are out of step and the roster would mispair them")
endif ()

set(_qb_ex_roster "# generated by examples/CMakeLists.txt — do not edit\n")
string(APPEND _qb_ex_roster "# built|<target>|<source>|<binary>   gated|<target>|<source>|<requires>\n")
math(EXPR _qb_ex_last "${_qb_ex_n} - 1")
if (_qb_ex_n GREATER 0)
    foreach (_i RANGE ${_qb_ex_last})
        list(GET _qb_ex_targets ${_i} _t)
        list(GET _qb_ex_sources ${_i} _s)
        string(APPEND _qb_ex_roster "built|${_t}|${_s}|$<TARGET_FILE:${_t}>\n")
    endforeach ()
endif ()
foreach (_g IN LISTS _qb_ex_gated)
    string(APPEND _qb_ex_roster "gated|${_g}\n")
endforeach ()
list(LENGTH _qb_ex_gated _qb_ex_n_gated)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/example-roster.txt"
     CONTENT "${_qb_ex_roster}")

qb_status_message("examples roster: ${_qb_ex_n} built, ${_qb_ex_n_gated} gated out -> ${CMAKE_CURRENT_BINARY_DIR}/example-roster.txt")