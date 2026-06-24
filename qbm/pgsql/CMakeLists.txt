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
# qb PostgreSQL Module Examples - CMake Configuration
#
# This file configures PostgreSQL client examples demonstrating queries,
# prepared statements, transactions, and data type handling.
# -----------------------------------------------------------------------------

project(qb_pgsql_examples
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "PostgreSQL module examples for the qb framework"
)

qb_status_message("Building qb PostgreSQL module examples")

# Define PostgreSQL examples to build
set(QB_PGSQL_EXAMPLES
        example1_basic_query
        example2_prepared_statements
        example3_transaction_management
        example4_data_types
        example5_error_handling
)

# Build each PostgreSQL example
foreach (EXAMPLE_NAME ${QB_PGSQL_EXAMPLES})
    qb_add_executable(
            NAME ${EXAMPLE_NAME}
            SOURCES ${EXAMPLE_NAME}.cpp
            DEPENDS qbm-pgsql
    )
endforeach ()

list(LENGTH QB_PGSQL_EXAMPLES QB_PGSQL_EXAMPLES_COUNT)
qb_status_message("PostgreSQL examples configured with ${QB_PGSQL_EXAMPLES_COUNT} targets") 