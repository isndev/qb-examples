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
# Pre-3.0 holding directory — one project the restructure has not placed yet.
#
# The other three moved to `05-services/` under derived target names. `file_monitor` did
# not, because the architecture does not move it: it RETIRES it into three different
# homes, and a retirement only lands with its replacements.
#
#   file_monitor/watcher.*   -> 02-io/08-timeouts-and-watchers, where "ev::stat polls one
#                               path and cannot name the changed file" is the LESSON rather
#                               than the disappointment
#   file_monitor/events.h    -> its relocation comment is the text 01-actors/03-event-payloads
#                               is to be written around
#   file_monitor/processor.*  -> 05-services/03-file-pipeline, which is where a processor
#                               that currently receives zero events belongs
#
# It keeps its pre-3.0 hand-written target name on purpose: a derived name would put it in
# a tier, and it is not in one.
# -----------------------------------------------------------------------------

project(qb_examples_core_io_holding
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Pre-3.0 core+I/O example awaiting its replacements"
)

if (NOT QB_PLATFORM_WINDOWS)
    add_subdirectory(file_monitor)
endif ()
