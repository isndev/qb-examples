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
# Tier 07 — full-stack applications (HTTP + WS + PostgreSQL + Redis)
#
# Single-binary projects, so neither takes a ROLE and the slug comes from the project
# directory alone:
#
#     07-applications/01-taskmanager/    ->  qb-example-applications-taskmanager
#     07-applications/02-auction-house/  ->  qb-example-applications-auction-house
#     07-applications/03-market-data-hub/->  qb-example-applications-market-data-hub
#
# 03-market-data-hub is the one this tier most needed: the other two are HTTP + Postgres +
# Redis CRUD services, so a corpus that stopped before it said qb is a web framework. It
# links qb-core and qb-io only — no HTTP, no SQL — and prints an end-to-end latency
# DISTRIBUTION rather than a headline number, because a single figure would be a claim
# about hardware it has never run on.
# -----------------------------------------------------------------------------

project(qb_examples_07_applications
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "Tier 07 — full-stack applications"
)

qb_status_message("Building qb examples — tier 07-applications")

add_subdirectory(01-taskmanager)
add_subdirectory(02-auction-house)
add_subdirectory(03-market-data-hub)
