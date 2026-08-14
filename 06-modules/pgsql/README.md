# QB PostgreSQL (`qbm-pgsql`) Module Examples

This directory contains a set of examples demonstrating the usage of the `qbm-pgsql` module, a C++20 asynchronous
PostgreSQL client integrated with the QB Actor Framework. All five use the **coroutine** API: every statement is
`co_await`ed and yields a `qb::pg::Reply<T>`.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Connection String](#connection-string)
- [Building the Examples](#building-the-examples)
- [Running the Examples](#running-the-examples)
- [Example Descriptions](#example-descriptions)
    - [`01-connect-and-query.cpp`](#01-connect-and-querycpp)
    - [`02-parameters.cpp`](#02-parameterscpp)
    - [`03-transactions.cpp`](#03-transactionscpp)
    - [`04-types.cpp`](#04-typescpp)
    - [`05-errors.cpp`](#05-errorscpp)

## Overview

These examples are designed to illustrate core functionalities of the `qbm-pgsql` library, including:

- Establishing connections to a PostgreSQL server.
- Executing simple and prepared SQL statements.
- Managing database transactions.
- Handling various PostgreSQL data types.
- Implementing error handling strategies.

Only `05-errors.cpp` is an actor application (`ErrorHandlingActor`, driven by `qb::Main`,
`05-errors.cpp:69`, `:261-267`). Examples **1–4 use no actors at all**: they are standalone `qb-io`
coroutines, scaffolded as `qb::io::async::init()` + `coro_scheduler().spawn(...)` + `run_until(...)`
(`01-connect-and-query.cpp:14-15`).

### The API in one block

```cpp
qb::pg::tcp::database db;

if (!co_await db.connect(PG_CONNECTION_STRING))   // yields bool
    co_return;

auto reply = co_await db.execute("SELECT version();");   // Reply<resultset>
if (reply.ok()) {
    const auto &rs = reply.result();
    if (!rs.empty())
        std::cout << rs[0][0].as<std::string>();
} else {
    std::cerr << reply.error().what();               // qb::pg::error::db_error
}

// Prepared statements
co_await db.prepare(NAME, "INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id;",
                    qb::pg::type_oid_sequence{qb::pg::oid::text, qb::pg::oid::text});
auto ins = co_await db.execute(NAME, qb::pg::params{name, email});   // Reply<resultset>

// Transactions — explicit and manual; nothing commits or rolls back for you
if (!(co_await db.begin()).ok())
    co_return;
// ... statements; on the first !ok() call `co_await db.rollback();` yourself ...
co_await db.commit();
```

There is **no** `.then()` / `.success()` / `.error()` continuation chain and no `tr.execute(...)` transaction object —
that API is gone, and error handling is now linear (`05-errors.cpp:19-25`). `db_error` carries `what()`,
`severity`, `code` (SQLSTATE string), `detail` and a structured `sqlstate` enum. Client-side conversion failures are
still **thrown**: `qb::pg::error::value_is_null` and `qb::pg::error::field_type_mismatch`, caught with `try`/`catch`
(`05-errors.cpp:224`, `:234`).

## Prerequisites

1. **PostgreSQL Server**: A running PostgreSQL server instance (version 9.6 or higher recommended).
2. **Database and User**: A PostgreSQL database and a user with privileges to connect, create tables, and perform
   DML/DDL operations on that database. The examples default to a database named `test` and a user `test` with password
   `test`.
3. **QB Framework**: The QB Actor Framework, including `qb-core`, `qb-io`, and `qbm-pgsql` modules, must be built.
4. **CMake**: 3.22 is the floor declared by `examples/CMakeLists.txt:25`, but the examples are built from the
   superproject, whose own floor is **3.24** (`CMakeLists.txt:2`, `qb/CMakeLists.txt:31`). Configuring through a
   `CMakePresets.json` preset needs **3.25** (the file is schema v6).
5. **C++20 Compiler**: the examples are coroutines; C++17 will not compile them.

## Connection String

Each example C++ file contains a `PG_CONNECTION_STRING` constant:

```cpp
// IMPORTANT: Replace with your actual PostgreSQL connection string
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";
```

**CRITICAL**: You **MUST** update this string to match your PostgreSQL server's host, port, database name, username, and
password before attempting to run any example.

The format is `schema://[user[:password]@]host[:port][database_name]`.

- `schema`: Can be `tcp` for plain TCP connections or `ssl` if you are using `qb::pg::tcp::ssl::database` and your
  server supports SSL.
- `[...]`: Optional parts. If database name is omitted, it might try to connect to a database with the same name as the
  user.

## Building the Examples

The provided `CMakeLists.txt` file is configured to build all examples.

1. **Navigate to the QB build directory**: This is the directory where you've built the main QB framework.
2. **Ensure examples are enabled in your main QB CMake configuration**: If you built QB with `QB_BUILD_EXAMPLES=ON` (or
   a similar option that includes module examples), these should be buildable.
3. **Build a specific example**:
   ```bash
   cmake --build . --target <example_name>
   # e.g.,
   cmake --build . --target qb-example-modules-pgsql-connect-and-query
   ```
4. **Build all qbm-pgsql examples**: If your CMake setup for `qbm-pgsql` is part of a larger QB build, they might be
   built automatically when building the `qbm-pgsql` target or all examples. Refer to your main QB build system if
   unsure.

If you are building these examples standalone (assuming `qbm-pgsql` is installed as a package):

```bash
cd examples/06-modules/pgsql
mkdir build
cd build
cmake .. 
make qb-example-modules-pgsql-connect-and-query # or any other example, or just 'make' for all
```

The executables will be placed in your CMake build system's binary output directory (e.g., `your_qb_build_dir/bin/` or
`examples/06-modules/pgsql/build/`).

## Running the Examples

Once built, you can run each example directly from its location in the build output directory:

```bash
./<example_name>
# e.g.,
./qb-example-modules-pgsql-connect-and-query
```

Make sure your PostgreSQL server is running and accessible with the connection string you've configured in the source
file.

## Example Descriptions

### `01-connect-and-query.cpp`

* **Purpose**: Demonstrates the most basic interaction with a PostgreSQL database: establishing a connection and
  executing a simple query.
* **Key Features**:
    * Standalone `qb-io` scaffolding — no actor, no `qb::Main`: `init()` + `coro_scheduler().spawn()` + `run_until()`
      (`01-connect-and-query.cpp:14-15`).
    * Creating a `qb::pg::tcp::database` client (`:44`).
    * `co_await db.connect(uri)` — yields `bool`; on failure `db.error().what()` says why (`:47-51`).
    * `co_await db.execute("SELECT version();")` — yields `Reply<resultset>`; no explicit transaction is opened
      (`:56`).
    * `reply.ok()` / `reply.result()` / `reply.error()`, then `rs[0][0].as<std::string>()` to read one field
      (`:57-64`).
    * A scope guard flips the `running` flag on **every** exit path so `run_until()` stops (`:37-42`).
* **Database Operations**:
    * `SELECT version();`

### `02-parameters.cpp`

* **Purpose**: Illustrates the use of prepared statements for enhanced performance and security.
* **Key Features**:
    * Creating the `users` table with a plain `co_await db.execute(sql)` (`02-parameters.cpp:124`) —
      DDL is not prepared.
    * Preparing the INSERT and SELECT with
      `co_await db.prepare(name, sql, qb::pg::type_oid_sequence{...})` → `Reply<PreparedQuery>` (`:129`, `:139`),
      specifying parameter type OIDs (`qb::pg::oid::text`, `qb::pg::oid::int4`).
    * Executing them by **name** with packed parameters:
        * `co_await db.execute(PREPARE_INSERT_USER, qb::pg::params{name, email})` (`:53`)
        * `co_await db.execute(PREPARE_SELECT_USER_BY_ID, qb::pg::params{new_id})` (`:70`)
    * Retrieving a generated id from the `RETURNING id` result set.
    * `std::optional` for nullable columns — `qb::pg::params` accepts `std::nullopt` transparently (`:151`).
    * Detecting a unique-constraint violation by SQLSTATE: `std::string(err.code) == "23505"` (`:57-58`).
    * A miss: selecting id `999` and finding an empty result set (`:157`).
    * Cleanup is an ordinary awaited statement at the end of the coroutine, not a destructor and not a blocking
      `.await()` (`:168`).
* **Database Operations**:
    * `CREATE TABLE IF NOT EXISTS users (...)`
    * `INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id;`
    * `SELECT id, name, email FROM users WHERE id = $1;`
    * `DROP TABLE IF EXISTS users;`

### `03-transactions.cpp` — **rewritten**

Its `@brief` promised savepoints and its body contained none (measured: zero calls to `savepoint`,
`rollback_savepoint` or `release_savepoint`); it had no `with_transaction`; and it ended by
checking the balances of three accounts it never created. It now covers all five shapes, each with
a gated verdict: manual `begin`/`commit`/`rollback`, `qb::pg::with_transaction` and its exact
exception contract (`transaction_abort` → rollback + failed `Reply`; anything else → rollback then
**rethrow**; nesting refused before a BEGIN is sent), SAVEPOINTs for partial rollback,
`transaction_mode` for isolation and READ ONLY, and `set_timeout` for a `statement_timeout` that
fails with SQLSTATE 57014. Note the spellings: `rollback_savepoint` and `release_savepoint`, not
`rollback_to` and `release`.

#### The original notes


* **Purpose**: Shows how to manage database transactions, including a simulated fund transfer scenario.
* **Key Features**:
    * **Explicit, manual** transaction control — there is no chain and no automatic commit/rollback. You write
      `co_await db.begin()` (`03-transactions.cpp:156`), then the statements, then
      `co_await db.commit()` (`:181`) or `co_await db.rollback()` (`:163`, `:174`) yourself, on the branch you decide.
    * Sequencing is ordinary control flow: check `reply.ok()` after each `co_await` and `rollback()` on the first
      failure (`:160-176`).
    * Ensuring accounts exist before updating them (insert, tolerate the unique violation).
    * A successful fund transfer: two `UPDATE`s inside one BEGIN/COMMIT (`:160`, `:171`).
    * A transfer that is **meant** to fail — a duplicate insert of `'Eve'` inside the transaction — followed by an
      explicit `ROLLBACK` (`:203-222`).
    * Reading and displaying account balances.
    * Data type: `DOUBLE PRECISION` for account balances.
* **Database Operations**:
    * `CREATE TABLE IF NOT EXISTS accounts (...)`
    * `INSERT INTO accounts (name, balance) VALUES ($1, $2) RETURNING id, name, balance;`
    * `UPDATE accounts SET balance = balance + $1 WHERE name = $2;`
    * `SELECT id, name, balance FROM accounts WHERE name = $1;`
    * `DELETE FROM accounts WHERE name = $1;` (Prepared, but not used in main flow)
    * `DROP TABLE IF EXISTS accounts;`

### `04-types.cpp`

* **Purpose**: Demonstrates inserting and retrieving various PostgreSQL data types, showcasing the mapping between C++
  types and PostgreSQL types supported by `qbm-pgsql`.
* **Key Features**:
    * Creating a table (`data_types_test`) with columns of many different PostgreSQL types (integers, text, boolean,
      numeric/decimal, float, date/time, UUID, bytea, JSON/JSONB, arrays).
    * Preparing an `INSERT` with placeholders for all supported types and specifying their OIDs, via
      `co_await db.prepare(name, sql, qb::pg::type_oid_sequence{...})`.
    * Preparing a `SELECT` statement to retrieve all columns.
    * Inserting sample data using C++ types like `int`, `short`, `long long`, `std::string`, `bool`, `float`, `double`,
      `qb::wall_time`, `qb::uuid`, `std::vector<char>` (for bytea), `qb::json`, `std::vector<int>`,
      `std::vector<std::string>`, and `std::optional<std::string>`.
    * Retrieving data using `row[column_name].as<ExpectedCppType>()`.
    * Displaying the retrieved data, including formatting for timestamps and byte arrays.
* **Database Operations**:
    * `CREATE TABLE IF NOT EXISTS data_types_test (...)` (with numerous data types)
    * `INSERT INTO data_types_test (...) VALUES ($1, $2, ..., $21) RETURNING id;`
    * `SELECT * FROM data_types_test WHERE id = $1;`
    * `DROP TABLE IF EXISTS data_types_test;`

### `05-errors.cpp`

* **Purpose**: Focuses on demonstrating how `qbm-pgsql` reports various database and client-side errors.
  **The only actor-based example here** — `ErrorHandlingActor : public qb::Actor` (`:60`) with
  `qb::io::async::task<bool> onInit()` that `co_await db.connect(...)`, added to a `qb::Main` engine (`:252-258`).
* **Key Features**:
    * A helper `printDbError` to display detailed information from a `qb::pg::error::db_error` (severity, SQLSTATE,
      message, detail) — `:104`.
    * **Scenario 1: Syntax Error**: Executing an intentionally malformed SQL query to trigger a `42601 (syntax_error)`.
    * **Scenario 2: Unique Constraint Violation**: Attempting to insert a duplicate value into a column with a `UNIQUE`
      constraint, triggering a `23505 (unique_violation)`.
    * **Scenario 3: Check Constraint Violation**: Attempting to insert data that violates a `CHECK` constraint (e.g.,
      negative quantity), triggering a `23514 (check_violation)`.
    * **Scenario 4: Client-Side Result Processing Errors**:
        * Attempting to retrieve a `NULL` database value using `.as<std::string>()` (non-optional type), expecting a
          `qb::pg::error::value_is_null` exception.
        * Attempting to retrieve an integer column using `.as<std::string>()` (type mismatch), expecting a
          `qb::pg::error::field_type_mismatch` exception.
    * Two different mechanisms, deliberately: server-side failures arrive as data — `if (!reply.ok())
      printDbError(..., reply.error())` — while client-side conversion failures are **thrown** and caught with
      `try`/`catch` (`:215`, `:225`).
    * Driving an SQL-issuing coroutine from a synchronous actor handler with `spawn(...)`.
* **Database Operations**:
    * `CREATE TABLE IF NOT EXISTS error_test_items (...)` (with `UNIQUE` and `CHECK` constraints)
    * `INSERT INTO error_test_items (name, quantity, description) VALUES ($1, $2, $3);`
    * `SELECT name, quantity, description FROM error_test_items WHERE name = $1;`
    * Deliberately malformed `SELEC * FRM non_existent_table;`
    * `DROP TABLE IF EXISTS error_test_items;`

---

These examples provide a solid foundation for understanding and utilizing the `qbm-pgsql` module in your C++
applications. Remember to adapt the connection settings and SQL schemas to your specific needs. 