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
# 06-modules/pgsql — PostgreSQL with qbm-pgsql
#
#     01-connect-and-query.cpp  ->  qb-example-modules-pgsql-connect-and-query
#     02-parameters.cpp         ->  qb-example-modules-pgsql-parameters
#     03-transactions.cpp       ->  qb-example-modules-pgsql-transactions
#     04-types.cpp              ->  qb-example-modules-pgsql-types
#     05-errors.cpp             ->  qb-example-modules-pgsql-errors
#     06-typed-rows.cpp         ->  qb-example-modules-pgsql-typed-rows
#     07-listen-notify.cpp      ->  qb-example-modules-pgsql-listen-notify
#     08-tls-and-limits.cpp     ->  qb-example-modules-pgsql-tls-and-limits
#     09-callbacks-and-await.cpp ->  qb-example-modules-pgsql-callbacks-and-await
#     10-streaming-results.cpp   ->  qb-example-modules-pgsql-streaming-results
#
# 09 and 10 close the two gaps this client had: the whole CALLBACK half of the API (every
# operation is declared twice, and only the co_await half was ever shown — so `then`/`success`/
# `error`, the three `discard_*` no-ops, `prepare_file` and `Transaction::await()` had zero call
# sites in the corpus), and `query_stream`, the server-side cursor that reads a result set larger
# than memory. 09 contains no `co_await` at all, which is the point of it.
#
# All ten are written. Two names in the architecture document do not exist in the tree and
# 06-typed-rows is written against the header instead: `first_as` and `views<Ts...>` are shipped
# as `one<Ts...>` and `rows<Ts...>` (qbm/pgsql/src/qbm/pgsql/resultset.h:214/230/248).
# -----------------------------------------------------------------------------

project(qb_examples_06_pgsql
        LANGUAGES CXX
        VERSION ${QB_FRAMEWORK_VERSION}
        DESCRIPTION "PostgreSQL module examples"
)

qb_status_message("Building qb examples — 06-modules/pgsql")

qb_example(SOURCE 01-connect-and-query.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 02-parameters.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 03-transactions.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 04-types.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 05-errors.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 06-typed-rows.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 07-listen-notify.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 08-tls-and-limits.cpp DEPENDS qbm-pgsql)
qb_example(SOURCE 09-callbacks-and-await.cpp DEPENDS qbm-pgsql TARGET_VAR _tgt_callbacks)
qb_example(SOURCE 10-streaming-results.cpp DEPENDS qbm-pgsql)

# One example reads an asset at runtime: 09-callbacks-and-await loads its statement from
# resources/sql/top-scores.sql, which is the whole point of Transaction::prepare_file(). It says
# "resources/sql/top-scores.sql" and nothing else; qb_stage_example_resources stages the tree next
# to the built executables so that plain relative path resolves on every generator and platform.
#
# The name comes back from TARGET_VAR rather than being retyped — the same reason
# 06-modules/http/CMakeLists.txt does it: a gated-out example leaves its variable UNSET, so the
# list holds only targets that were really created.
if (DEFINED _tgt_callbacks)
    qb_stage_example_resources(
            STAGE_NAME qb_pgsql_examples_resources
            TARGETS ${_tgt_callbacks})
endif ()
