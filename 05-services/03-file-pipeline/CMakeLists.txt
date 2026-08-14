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
# 05-services/03-file-pipeline — reading files off the loop
#
# A single-binary project, so it takes no ROLE and the slug comes from the project
# directory alone:
#
#     05-services/03-file-pipeline  ->  qb-example-services-file-pipeline
# -----------------------------------------------------------------------------

project(qb_examples_05_file_pipeline
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "File processing pipeline — actors, workers and batching"
)

qb_status_message("Building qb examples — 05-services/03-file-pipeline")

qb_example(SOURCE main.cpp DEPENDS qb-core)
