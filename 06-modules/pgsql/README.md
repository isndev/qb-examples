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
    - [`03-transactions.cpp`](#03-transactionscpp--rewritten)
    - [`04-types.cpp`](#04-typescpp)
    - [`05-errors.cpp`](#05-errorscpp)
    - [`06-typed-rows.cpp`](#6-typed-rows-06-typed-rowscpp--new)
    - [`07-listen-notify.cpp`](#7-listen--notify-07-listen-notifycpp--new)
    - [`08-tls-and-limits.cpp`](#8-tls-and-limits-08-tls-and-limitscpp--new)
    - [`09-callbacks-and-await.cpp`](#9-callbacks-and-await-09-callbacks-and-awaitcpp--new)
    - [`10-streaming-results.cpp`](#10-streaming-results-10-streaming-resultscpp--new)

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

Build from the **superproject root**, which force-enables `QB_BUILD_EXAMPLES`:

```bash
cmake --preset release
cmake --build --preset release --target qb-example-modules-pgsql-connect-and-query
```

There is **no standalone build**. This directory's `CMakeLists.txt` has no `cmake_minimum_required`
and calls `qb_example()`, which only qb defines — `cmake -S examples/06-modules/pgsql` fails before it
compiles anything. The same is true of the whole `isndev/qb-examples` repository; see
[`examples/README.md`](../../README.md).

The executables land in `build/presets/<preset>/examples/06-modules/pgsql/`.

## Running the Examples

Once built, run each example from the build output directory:

```bash
./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-connect-and-query
```

Make sure your PostgreSQL server is running and accessible with the connection string you've configured in the source
file. **All eight need a live server**: `dev/agent/run-examples.py` records `needs = postgres` for every
target here and reports a SKIP, never a pass, when nothing answers.

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

* **Purpose**: every shape the word "transaction" covers in this client, each proved against a live
  server rather than described. The worked scenario is still a fund transfer, but between two rows of
  a table the program creates and drops itself.
* **Key Features**:
    * **Manual** control — `co_await db.begin()`, the statements, then `commit()` or `rollback()`
      yourself, on the branch you decide. Ordinary control flow: check `reply.ok()` after each
      `co_await`. The example prints the trap that makes this shape dangerous — a `BEGIN` whose
      `UPDATE` failed will happily `COMMIT`, because nothing checked.
    * **`qb::pg::with_transaction`** and its exact exception contract: `transaction_abort` → rollback
      plus a failed `Reply`; any other exception → rollback then **rethrow**; a nested call is refused
      before a `BEGIN` is even sent.
    * **SAVEPOINT** for partial rollback — note the spellings, `rollback_savepoint` and
      `release_savepoint`, not `rollback_to` and `release`.
    * **`transaction_mode`** for isolation level and READ ONLY.
    * **`set_timeout`** for a `statement_timeout` that fails with SQLSTATE 57014.
* **Database Operations** — one table, created and dropped by the program:
    * `CREATE TABLE IF NOT EXISTS qb_tx_accounts (...)`
    * `INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2), ($3, $4)`
    * `UPDATE qb_tx_accounts SET balance = balance ± $1 WHERE name = $2`
    * `SELECT balance FROM qb_tx_accounts WHERE name = $1` and `SELECT SUM(balance) FROM qb_tx_accounts`
    * `DROP TABLE IF EXISTS qb_tx_accounts;`
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-transactions`

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

---

### 6. Typed Rows (`06-typed-rows.cpp`) — **new**

* **Purpose**: reading a result set without writing a loop, and the two runtime failures the by-name
  loop invites (a misspelled column name is a string that compiles; `as<T>()` on a NULL throws unless
  `T` is a `std::optional`). Both move into ONE place — the tuple you declare.
* **API**: `row::as<std::tuple<...>>()`, `resultset::one<Ts...>()` (the single-row query, as an
  `optional<tuple>`), `all<Ts...>()` (an eager vector you own) versus `rows<Ts...>()` (a lazy view
  that BORROWS the result set), `field::text()` / `field::view()` for a read with no copy, and
  `resultset::json()` for the whole set at once.
* **Names**: the architecture document called these `first_as` and `views<Ts...>`. Neither exists;
  the shipped names are `one` / `all` / `rows` (`qbm/pgsql/src/qbm/pgsql/resultset.h:214/230/248`).
* **Two things measured while writing it**: the tuple is POSITIONAL over the SELECT list with no name
  matching, so a mismatch throws at RUN time; and `resultset::json()` renders every scalar as a JSON
  STRING (only NULL is typed), because it emits the text-format wire bytes rather than re-typing them.
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-typed-rows`

---

### 7. LISTEN / NOTIFY (`07-listen-notify.cpp`) — **new**

* **Purpose**: PostgreSQL as an event bus — the replacement for a `SELECT ... WHERE updated_at > $1`
  poll. A `notify_co_consumer` you `co_await`, and a TRIGGER that calls `pg_notify()` so the writer
  does not have to know a listener exists.
* **API**: `qb::pg::tcp::notify_co_consumer`, `listen`/`unlisten`/`unlisten_all`, `notify`,
  `on_notify` + `on_notify_dropped`, `receive()`, `notify_channel_capacity()`.
* **The four rules it measures**: delivery is TRANSACTIONAL (a NOTIFY inside a transaction arrives at
  COMMIT, never after a rollback); it is NOT a queue (one sent while nobody listens is gone); the
  payload is capped at 8000 bytes, so send an identifier and let the reader fetch the row; and the
  subscription belongs to the CONNECTION, so a reconnect subscribes you to nothing.
* **And a limit worth knowing**: after ANY disconnect this consumer's `co_await receive()` is DEAD.
  `on_pg_notify_consumer_disconnected` closes its internal channel and `qb::io::async::channel::close()`
  is terminal, so `receive()` answers `nullopt` for ever while `on_notify()` keeps firing and every
  later notification is handed to the drop handler. Build a NEW consumer after a drop.
* **Do not call `disconnect()` from a coroutine**: it ends with
  `qb::io::async::listener::current.run(EVRUN_NOWAIT)` (`pgsql.h:2530`), and pumping the loop from
  inside a coroutine re-enters `CoroutineScheduler::run_ready`, whose assert catches exactly that
  (SIGABRT under the `sanitize` preset; silent re-entrancy under `release`). This example drops the
  link with `pg_terminate_backend` from the publisher instead — which is also what a failover does.
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-listen-notify`

---

### 8. TLS and Limits (`08-tls-and-limits.cpp`) — **new**

* **Purpose**: PostgreSQL has NO TLS port. A client connects in cleartext, sends an 8-byte SSLRequest
  and reads one byte back; only then is the socket upgraded. So the DSN stays `tcp://` and the only
  change is the client TYPE — `qb::pg::tcp::ssl::database`, which selects the `stcp` transport and
  hands it to `starttls_connect` with `postgres_ssl_negotiator`.
* **API**: `qb::pg::connection_options` + `ssl_verify_mode`, `connect(options)`, `application_name`,
  `set_timeout`/`get_timeout`, `qb::pg::sqlstate::query_canceled`.
* **What it says out loud**: the default `ssl_verify_mode::none` is ENCRYPTED BUT UNAUTHENTICATED
  (libpq's `sslmode=require`) — it defeats a passive listener and nothing else. And `set_timeout` is
  TRANSACTION-scoped: it rides with the next `BEGIN` as `SET LOCAL statement_timeout`, so it must be
  called BEFORE `begin()`, and a query it cancels is SQLSTATE 57014 rather than a dropped connection.
* **It MEASURES the server rather than asserting about it**: whether TLS is available is the
  operator's decision (`ssl = on`), so sections 1 and 2 print what they found in either direction. The
  proof is `pg_stat_ssl` — the server's own view of the connection, not the client's opinion.
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-tls-and-limits`

---

### 9. Callbacks and Await (`09-callbacks-and-await.cpp`) — **new**

* **Purpose**: `qbm-pgsql` has TWO complete APIs over one connection — every operation is declared
  twice, once returning `pg_reply_awaiter<T>` and once taking callbacks — and only the `co_await`
  half had ever been demonstrated. This file is the other half, and it contains no `co_await` at
  all: it is what you write from a `main()`, a migration tool, a test fixture, or any thread that is
  not running an event loop.
* **API**: `execute(sql, on_success, on_error)`, `.then()` / `.success()` / `.error()`,
  `qb::pg::discard_query` / `discard_error` / `discard_prepare`, `prepare()`, `prepare_file()`,
  `execute(name, qb::pg::params{...}, ...)`, `Transaction::await()` and its free-function form
  `qb::pg::await(db)`, plus the one bridge synchronous code needs — `qb::io::async::run_sync(db.connect(...))`,
  because `connect` is the single operation with no callback overload.
* **The chain is a QUEUE, not a sequence of calls**: `.then()` appends a node that fires only if the
  step before it SUCCEEDED, `.error()` one that fires only if it failed, and neither runs anything.
  `await()` turns the loop until that queue is empty. So the whole chain is built for free and
  executes at exactly one point — which is what makes a batch of statements cost one round trip's
  waiting instead of one per statement. The program prints that nothing had run before the drain.
* **Why there are THREE discards and not one**: the callback overloads take the handler by value and
  call it unconditionally, with no per-reply branch, so a handler is never absent — and `prepare()`
  wants `(Transaction&, PreparedQuery const&)` where `execute()` wants `(Transaction&, results)`.
  Reaching for `nullptr` is a compile error, which is the correct outcome.
* **`prepare_file()`** reads `resources/sql/top-scores.sql` — staged next to the binary by
  `qb_stage_example_resources()` — so the statement is reviewable, diffable and lintable AS SQL
  instead of surviving as an escaped C++ literal. The parameter OIDs still come from the call site.
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-callbacks-and-await`

---

### 10. Streaming Results (`10-streaming-results.cpp`) — **new**

* **Purpose**: the result set that does not fit. `co_await db.query("SELECT * FROM huge")` reads
  every `DataRow` into memory before resuming you; `query_stream()` declares a server-side `CURSOR`
  and `FETCH`es one batch at a time, so client memory is `batch_size` rows regardless of how many
  the query matches. `LIMIT`/`OFFSET` paging is not the alternative — `OFFSET n` makes the server
  walk and discard n rows every time, so a paged scan is quadratic.
* **API**: `query_stream(sql, batch_size, on_row)` yielding `qb::pg::Reply<void>`, beside `query()`,
  `begin()` / `commit()` / `in_transaction()` and `qb::io::async::when_all`.
* **The row is a VIEW**: it points INTO the batch buffer, which the next `FETCH` overwrites. It is
  valid for the duration of the call and not one instruction longer — copy out a value (`as<T>`
  copies), never keep the row, a `field`, or a `string_view` taken from one.
* **Cursors need a transaction, and PostgreSQL allows one per session.** Inside a transaction you
  opened, `query_stream` declares the cursor there and closes only the cursor; outside one it opens
  its own `BEGIN`…`COMMIT`. Two streams overlapping on one connection therefore SHARE that block —
  the first opens it, the last to finish ends it, and it is rolled back if either failed. Each gets
  its own cursor NAME, which is what stops them from closing each other's; the program runs two at
  once through `when_all` and checks both answers, because without that they would both read zero.
* **Failure is a Reply, not an exception** — the exception path belongs to YOUR callback, and one
  that throws closes the cursor, rethrows, and leaves the connection usable.
* **Run**: `./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-streaming-results`
