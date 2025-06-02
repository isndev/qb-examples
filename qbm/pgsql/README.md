# QB PostgreSQL (`qbm-pgsql`) Module Examples

This directory contains a set of examples demonstrating the usage of the `qbm-pgsql` module, a C++17 asynchronous PostgreSQL client integrated with the QB Actor Framework.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Connection String](#connection-string)
- [Building the Examples](#building-the-examples)
- [Running the Examples](#running-the-examples)
- [Example Descriptions](#example-descriptions)
  - [`example1_basic_query.cpp`](#example1_basic_querycpp)
  - [`example2_prepared_statements.cpp`](#example2_prepared_statementscpp)
  - [`example3_transaction_management.cpp`](#example3_transaction_managementcpp)
  - [`example4_data_types.cpp`](#example4_data_typescpp)
  - [`example5_error_handling.cpp`](#example5_error_handlingcpp)

## Overview

These examples are designed to illustrate core functionalities of the `qbm-pgsql` library, including:
- Establishing connections to a PostgreSQL server.
- Executing simple and prepared SQL statements.
- Managing database transactions.
- Handling various PostgreSQL data types.
- Implementing error handling strategies.

Each example is a self-contained `qb::Actor` application that interacts with a PostgreSQL database.

## Prerequisites

1.  **PostgreSQL Server**: A running PostgreSQL server instance (version 9.6 or higher recommended).
2.  **Database and User**: A PostgreSQL database and a user with privileges to connect, create tables, and perform DML/DDL operations on that database. The examples default to a database named `test` and a user `test` with password `test`.
3.  **QB Framework**: The QB Actor Framework, including `qb-core`, `qb-io`, and `qbm-pgsql` modules, must be built.
4.  **CMake**: CMake version 3.14 or higher is required to build the examples.
5.  **C++17 Compiler**: A C++17 compatible compiler (e.g., GCC 7+, Clang 5+).

## Connection String

Each example C++ file contains a `PG_CONNECTION_STRING` constant:

```cpp
// IMPORTANT: Replace with your actual PostgreSQL connection string
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";
```

**CRITICAL**: You **MUST** update this string to match your PostgreSQL server's host, port, database name, username, and password before attempting to run any example.

The format is `schema://[user[:password]@]host[:port][database_name]`.
- `schema`: Can be `tcp` for plain TCP connections or `ssl` if you are using `qb::pg::tcp::ssl::database` and your server supports SSL.
- `[...]`: Optional parts. If database name is omitted, it might try to connect to a database with the same name as the user.

## Building the Examples

The provided `CMakeLists.txt` file is configured to build all examples.

1.  **Navigate to the QB build directory**: This is the directory where you've built the main QB framework.
2.  **Ensure examples are enabled in your main QB CMake configuration**: If you built QB with `QB_BUILD_EXAMPLES=ON` (or a similar option that includes module examples), these should be buildable.
3.  **Build a specific example**:
    ```bash
    cmake --build . --target <example_name>
    # e.g.,
    cmake --build . --target example1_basic_query
    ```
4.  **Build all qbm-pgsql examples**: If your CMake setup for `qbm-pgsql` is part of a larger QB build, they might be built automatically when building the `qbm-pgsql` target or all examples. Refer to your main QB build system if unsure.

If you are building these examples standalone (assuming `qbm-pgsql` is installed as a package):
```bash
cd examples/qbm/pgsql
mkdir build
cd build
cmake .. 
make example1_basic_query # or any other example, or just 'make' for all
```

The executables will be placed in your CMake build system's binary output directory (e.g., `your_qb_build_dir/bin/` or `examples/qbm/pgsql/build/`).

## Running the Examples

Once built, you can run each example directly from its location in the build output directory:
```bash
./<example_name>
# e.g.,
./example1_basic_query
```
Make sure your PostgreSQL server is running and accessible with the connection string you've configured in the source file.

## Example Descriptions

### `example1_basic_query.cpp`

*   **Purpose**: Demonstrates the most basic interaction with a PostgreSQL database: establishing a connection and executing a simple query.
*   **Key Features**:
    *   Creating a `qb::pg::tcp::database` client.
    *   Connecting to the database using a connection string.
    *   Basic error checking for the connection.
    *   Starting a simple transaction with `_db_connection->begin(...)`.
    *   Executing a single SQL query (`SELECT version();`) using `tr.execute(...)`.
    *   Retrieving and displaying a single field from the result set.
    *   Graceful shutdown of the actor.
*   **Database Operations**:
    *   `SELECT version();`

### `example2_prepared_statements.cpp`

*   **Purpose**: Illustrates the use of prepared statements for enhanced performance and security.
*   **Key Features**:
    *   Creating a table (`users`) if it doesn't exist.
    *   Preparing SQL statements (`CREATE TABLE`, `INSERT`, `SELECT`) using `tr.prepare(...)`.
        *   Specifying parameter type OIDs (e.g., `qb::pg::oid::text`, `qb::pg::oid::int4`).
    *   Executing prepared statements with parameters:
        *   `_db_connection->execute(PREPARE_INSERT_USER, {name, email}, ...)`
        *   `_db_connection->execute(PREPARE_SELECT_USER_BY_ID, {user_id}, ...)`
    *   Handling results from prepared statements, including retrieving generated IDs.
    *   Using `std::optional` for nullable database columns (e.g., `email`).
    *   Basic error handling for unique constraint violations during insert.
    *   Cleaning up (dropping the table) in the destructor using a synchronous `.await()` for simplicity in cleanup.
*   **Database Operations**:
    *   `CREATE TABLE IF NOT EXISTS users (...)`
    *   `INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id;`
    *   `SELECT id, name, email FROM users WHERE id = $1;`
    *   `DROP TABLE IF EXISTS users;`

### `example3_transaction_management.cpp`

*   **Purpose**: Shows how to manage database transactions, including a simulated fund transfer scenario.
*   **Key Features**:
    *   Full transaction lifecycle: `_db_connection->begin(...)`, followed by a chain of operations, and automatic commit/rollback based on the success of the chain.
    *   Using `tr.then(...)` to sequence operations within a transaction.
    *   Using `tr.success(...)` and `tr.error(...)` for specific outcomes of the transaction chain.
    *   Demonstrating a common pattern: ensuring accounts exist (insert if not, ignore if unique violation) before attempting updates.
    *   Simulating a successful fund transfer between two accounts.
    *   Simulating a transaction that is expected to fail (e.g., due to a unique constraint violation during an intermediate step), leading to a rollback.
    *   Reading and displaying account balances.
    *   Data type: `DOUBLE PRECISION` for account balances.
*   **Database Operations**:
    *   `CREATE TABLE IF NOT EXISTS accounts (...)`
    *   `INSERT INTO accounts (name, balance) VALUES ($1, $2) RETURNING id, name, balance;`
    *   `UPDATE accounts SET balance = balance + $1 WHERE name = $2;`
    *   `SELECT id, name, balance FROM accounts WHERE name = $1;`
    *   `DELETE FROM accounts WHERE name = $1;` (Prepared, but not used in main flow)
    *   `DROP TABLE IF EXISTS accounts;`

### `example4_data_types.cpp`

*   **Purpose**: Demonstrates inserting and retrieving various PostgreSQL data types, showcasing the mapping between C++ types and PostgreSQL types supported by `qbm-pgsql`.
*   **Key Features**:
    *   Creating a table (`data_types_test`) with columns of many different PostgreSQL types (integers, text, boolean, numeric/decimal, float, date/time, UUID, bytea, JSON/JSONB, arrays).
    *   Preparing an `INSERT` statement with placeholders for all supported types and specifying their OIDs.
    *   Preparing a `SELECT` statement to retrieve all columns.
    *   Inserting sample data using C++ types like `int`, `short`, `long long`, `std::string`, `bool`, `float`, `double`, `qb::Timestamp`, `qb::uuid`, `std::vector<char>` (for bytea), `qb::json`, `std::vector<int>`, `std::vector<std::string>`, and `std::optional<std::string>`.
    *   Retrieving data using `row[column_name].as<ExpectedCppType>()`.
    *   Displaying the retrieved data, including formatting for timestamps and byte arrays.
*   **Database Operations**:
    *   `CREATE TABLE IF NOT EXISTS data_types_test (...)` (with numerous data types)
    *   `INSERT INTO data_types_test (...) VALUES ($1, $2, ..., $21) RETURNING id;`
    *   `SELECT * FROM data_types_test WHERE id = $1;`
    *   `DROP TABLE IF EXISTS data_types_test;`

### `example5_error_handling.cpp`

*   **Purpose**: Focuses on demonstrating how `qbm-pgsql` reports various database and client-side errors.
*   **Key Features**:
    *   A helper function `printDbError` to display detailed information from `qb::pg::error::db_error` objects (severity, SQLSTATE, message, detail, hint).
    *   **Scenario 1: Syntax Error**: Executing an intentionally malformed SQL query to trigger a `42601 (syntax_error)`.
    *   **Scenario 2: Unique Constraint Violation**: Attempting to insert a duplicate value into a column with a `UNIQUE` constraint, triggering a `23505 (unique_violation)`.
    *   **Scenario 3: Check Constraint Violation**: Attempting to insert data that violates a `CHECK` constraint (e.g., negative quantity), triggering a `23514 (check_violation)`.
    *   **Scenario 4: Client-Side Result Processing Errors**:
        *   Attempting to retrieve a `NULL` database value using `.as<std::string>()` (non-optional type), expecting a `qb::pg::error::value_is_null` exception.
        *   Attempting to retrieve an integer column using `.as<std::string>()` (type mismatch), expecting a `qb::pg::error::field_type_mismatch` exception.
    *   Utilizing the error callbacks in `tr.execute(...)` and `_db_connection->begin(...)`.
*   **Database Operations**:
    *   `CREATE TABLE IF NOT EXISTS error_test_items (...)` (with `UNIQUE` and `CHECK` constraints)
    *   `INSERT INTO error_test_items (name, quantity, description) VALUES ($1, $2, $3);`
    *   `SELECT name, quantity, description FROM error_test_items WHERE name = $1;`
    *   Deliberately malformed `SELEC * FRM non_existent_table;`
    *   `DROP TABLE IF EXISTS error_test_items;`

---

These examples provide a solid foundation for understanding and utilizing the `qbm-pgsql` module in your C++ applications. Remember to adapt the connection settings and SQL schemas to your specific needs. 