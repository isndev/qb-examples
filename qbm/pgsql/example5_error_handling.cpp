/**
 * @file examples/qbm/pgsql/example5_error_handling.cpp
 * @example qbm-pgsql: Error Handling (coroutine API)
 *
 * @brief Demonstrates PostgreSQL error handling with the modern **coroutine** API.
 *
 * @details
 * Every SQL statement returns a `qb::pg::Reply<qb::pg::results>` that you `co_await`. The reply
 * exposes `ok()` (statement succeeded), `result()` (the typed `qb::pg::results` resultset), and
 * `error()` (a `qb::pg::error::db_error` describing the failure). The old `.then()/.success()/
 * .error()` continuation chain is gone — error handling is now linear:
 *
 *     auto reply = co_await db.execute(...);
 *     if (reply.ok()) { use reply.result(); }
 *     else            { inspect reply.error(); }
 *
 * `db_error` carries: `what()` (message), `severity`, `code` (SQLSTATE string), `detail`, and the
 * structured `sqlstate` enum. Client-side conversion failures (NULL into a non-optional, or a type
 * mismatch from `.as<T>()`) are still thrown as `qb::pg::error::value_is_null` /
 * `qb::pg::error::field_type_mismatch` and are caught with try/catch.
 *
 * The example reproduces four error categories:
 * 1. Server syntax error (SQLSTATE 42601).
 * 2. Unique constraint violation (SQLSTATE 23505).
 * 3. Check constraint violation (SQLSTATE 23514).
 * 4. Client-side result-processing errors (NULL access + type mismatch).
 *
 * QB/QBM pgsql features demonstrated:
 * - `qb::io::async::task<bool> onInit()` — coroutine actor init with `co_await db.connect()`.
 * - `co_await db.execute(...)` / `co_await db.prepare(...)` returning `qb::pg::Reply<T>`.
 * - `Reply::ok()`, `Reply::result()`, `Reply::error()` and the `db_error` field set.
 * - Driving an SQL-issuing coroutine from a sync handler via `spawn(...)`.
 */
#include <qbm/pgsql/pgsql.h>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/async/coroutine/utils.h>
#include <qb/main.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

// IMPORTANT: Replace with your actual PostgreSQL connection string.
const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

const char *ERROR_TEST_TABLE_SQL = "CREATE TABLE IF NOT EXISTS error_test_items ("
                                   "id SERIAL PRIMARY KEY, "
                                   "name TEXT NOT NULL UNIQUE, "
                                   "quantity INT CHECK (quantity >= 0), "
                                   "description TEXT"
                                   ");";

const char *PREPARE_INSERT_ERROR_ITEM         = "insert_error_item_stmt_v5";
const char *PREPARE_SELECT_ERROR_ITEM_BY_NAME = "select_error_item_by_name_stmt_v5";

class ErrorHandlingActor : public qb::Actor {
public:
    ErrorHandlingActor() = default;

    ~ErrorHandlingActor() override = default;

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "ErrorHandlingActor [" << id() << "] onInit." << std::endl;
        registerEvent<qb::KillEvent>(*this);

        _db_connection = std::make_unique<qb::pg::tcp::database>();
        if (!co_await _db_connection->connect(std::string(PG_CONNECTION_STRING))) {
            auto &err = _db_connection->error();
            qb::io::cerr() << "Failed to connect to PostgreSQL: " << err.what() << std::endl;
            qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            co_return false;
        }
        qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;

        if (!co_await initializeSchemaAndStatements())
            co_return false;

        // Run the error scenarios, then schedule the actor's shutdown + cleanup.
        //
        // Both helpers are member coroutines that touch `_db_connection` after every
        // `co_await`, and the final line uses `this`. That is safe by OWNERSHIP, not by
        // `spawn`'s scope: `spawn` cancels only at scope-routed suspensions (`ctx.sleep`,
        // `ctx.cancellation_point`, `ctx.until_cancelled`, `ctx.cancellable`), and a pgsql
        // awaiter is not one — it registers nothing with the cancellation token. What protects
        // these bodies is that `_db_connection` is a MEMBER: `~Actor` destroys the database
        // with its pending queries, `Transaction::~Transaction` pops them WITHOUT running
        // `on_error`, and the completion hook is discarded uninvoked — so the coroutine never
        // resumes. Measured: an actor killed while parked on `SELECT pg_sleep(3)` never
        // resumes and ASan stays silent. An orphaned frame, not a use-after-free.
        //
        // Note the asymmetry, also measured: calling `_db->disconnect()` BEFORE `kill()` does
        // the opposite — it fails every pending query synchronously and pumps the loop, so the
        // coroutine resumes *inside* `disconnect()` while the actor is still alive. Safe, but
        // reentrant: that body runs to completion in the middle of the shutdown handler.
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            co_await runErrorScenarios();
            co_await cleanupDatabase();
            qb::io::cout() << "\nError scenarios complete. Shutting down." << std::endl;
            this->push<qb::KillEvent>(this->id());
        });

        co_return true;
    }

    void
    on(const qb::KillEvent &) {
        qb::io::cout() << "ErrorHandlingActor received KillEvent." << std::endl;
        kill();
    }

private:
    // Report a db_error using the current API surface:
    // what()/severity/code(SQLSTATE string)/detail + structured sqlstate enum.
    void
    printDbError(const std::string &context, const qb::pg::error::db_error &err) {
        qb::io::cerr() << "--- ERROR in " << context << " ---" << std::endl;
        qb::io::cerr() << "Message: " << err.what() << std::endl;
        qb::io::cerr() << "Severity: " << err.severity << std::endl;
        qb::io::cerr() << "SQLSTATE (Code): " << err.code << " (Enum: " << static_cast<int>(err.sqlstate) << ")" << std::endl;
        if (!err.detail.empty())
            qb::io::cerr() << "Detail: " << err.detail << std::endl;
        qb::io::cerr() << "-------------------------" << std::endl;
    }

    qb::io::async::task<bool>
    initializeSchemaAndStatements() {
        qb::io::cout() << "Initializing error_test_items table and preparing statements..." << std::endl;

        auto create = co_await _db_connection->execute(ERROR_TEST_TABLE_SQL);
        if (!create.ok()) {
            printDbError("Schema Creation", create.error());
            co_return false;
        }

        auto prep_insert = co_await _db_connection->prepare(PREPARE_INSERT_ERROR_ITEM,
                                                            "INSERT INTO error_test_items (name, quantity, description) VALUES ($1, $2, $3);",
                                                            {qb::pg::oid::text, qb::pg::oid::int4, qb::pg::oid::text});
        if (!prep_insert.ok()) {
            printDbError("Prepare Insert Statement", prep_insert.error());
            co_return false;
        }

        auto prep_select =
            co_await _db_connection->prepare(PREPARE_SELECT_ERROR_ITEM_BY_NAME,
                                             "SELECT name, quantity, description FROM error_test_items WHERE name = $1;", {qb::pg::oid::text});
        if (!prep_select.ok()) {
            printDbError("Prepare Select Statement", prep_select.error());
            co_return false;
        }

        qb::io::cout() << "Error test schema and statements initialized." << std::endl;
        co_return true;
    }

    qb::io::async::task<void>
    runErrorScenarios() {
        qb::io::cout() << "\n--- Running Error Handling Scenarios ---" << std::endl;

        // Scenario 1: Syntax Error (expected SQLSTATE 42601, syntax_error).
        {
            qb::io::cout() << "\nScenario 1: Executing query with syntax error..." << std::endl;
            auto reply = co_await _db_connection->execute("SELEC * FRM non_existent_table;");
            if (reply.ok())
                qb::io::cerr() << "UNEXPECTED SUCCESS: Syntax error query somehow succeeded." << std::endl;
            else
                printDbError("Syntax Error Query", reply.error());
        }

        // Scenario 2: Unique Constraint Violation (expected SQLSTATE 23505, unique_violation).
        {
            qb::io::cout() << "\nScenario 2: Unique constraint violation..." << std::endl;
            auto first = co_await _db_connection->execute(PREPARE_INSERT_ERROR_ITEM,
                                                          qb::pg::params{std::string("UniqueItem"), 10, std::string("First instance")});
            if (!first.ok()) {
                printDbError("Initial Insert for Unique Violation", first.error());
            } else {
                qb::io::cout() << "Attempting to insert duplicate 'UniqueItem'..." << std::endl;
                auto dup = co_await _db_connection->execute(
                    PREPARE_INSERT_ERROR_ITEM, qb::pg::params{std::string("UniqueItem"), 20, std::string("Second instance - should fail")});
                if (dup.ok())
                    qb::io::cerr() << "UNEXPECTED SUCCESS: Duplicate insert somehow succeeded." << std::endl;
                else
                    printDbError("Unique Constraint Violation", dup.error());
            }
        }

        // Scenario 3: Check Constraint Violation (expected SQLSTATE 23514, check_violation).
        {
            qb::io::cout() << "\nScenario 3: Check constraint violation (negative quantity)..." << std::endl;
            auto reply = co_await _db_connection->execute(
                PREPARE_INSERT_ERROR_ITEM, qb::pg::params{std::string("CheckItem"), -5, std::string("Negative quantity - should fail")});
            if (reply.ok())
                qb::io::cerr() << "UNEXPECTED SUCCESS: Insert with negative quantity succeeded." << std::endl;
            else
                printDbError("Check Constraint Violation", reply.error());
        }

        // Scenario 4: Client-side result-processing errors.
        // - Reading a NULL field as a non-optional std::string -> value_is_null.
        // - Reading an INT field as std::string -> field_type_mismatch.
        {
            qb::io::cout() << "\nScenario 4: Client-side result processing errors..." << std::endl;
            auto inserted = co_await _db_connection->execute(
                PREPARE_INSERT_ERROR_ITEM,
                qb::pg::params{std::string("ClientErrorItem"), 5, std::optional<std::string>()}); // description is NULL
            if (!inserted.ok()) {
                printDbError("Insert for Client Errors", inserted.error());
                co_return;
            }

            auto selected = co_await _db_connection->execute(PREPARE_SELECT_ERROR_ITEM_BY_NAME, qb::pg::params{std::string("ClientErrorItem")});
            if (!selected.ok()) {
                printDbError("Select for Client Errors", selected.error());
                co_return;
            }
            if (selected.result().empty()) {
                qb::io::cerr() << "ClientErrorItem not found." << std::endl;
                co_return;
            }

            const auto &row = selected.result()[0];
            try {
                qb::io::cout() << "Attempting to get NULL description as non-optional string..." << std::endl;
                [[maybe_unused]] std::string desc = row["description"].as<std::string>(); // Should throw
                qb::io::cerr() << "UNEXPECTED: Retrieved NULL as non-optional string." << std::endl;
            } catch (const qb::pg::error::value_is_null &e) {
                qb::io::cout() << "CAUGHT EXPECTED: qb::pg::error::value_is_null: " << e.what() << std::endl;
            } catch (const std::exception &e) {
                qb::io::cerr() << "UNEXPECTED EXCEPTION (value_is_null): " << e.what() << std::endl;
            }

            try {
                qb::io::cout() << "Attempting to get quantity (INT) as std::string..." << std::endl;
                [[maybe_unused]] std::string qty_str = row["quantity"].as<std::string>(); // Should throw
                qb::io::cerr() << "UNEXPECTED: Retrieved INT quantity as string." << std::endl;
            } catch (const qb::pg::error::field_type_mismatch &e) {
                qb::io::cout() << "CAUGHT EXPECTED: qb::pg::error::field_type_mismatch: " << e.what() << std::endl;
            } catch (const std::exception &e) {
                qb::io::cerr() << "UNEXPECTED EXCEPTION (field_type_mismatch): " << e.what() << std::endl;
            }
        }

        qb::io::cout() << "\nError scenarios initiated. Check output for results." << std::endl;
        co_return;
    }

    qb::io::async::task<void>
    cleanupDatabase() {
        qb::io::cout() << "Cleaning up error_test_items table..." << std::endl;
        auto reply = co_await _db_connection->execute("DROP TABLE IF EXISTS error_test_items;");
        if (reply.ok())
            qb::io::cout() << "error_test_items table dropped." << std::endl;
        else
            printDbError("Cleanup Database", reply.error());
        co_return;
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int
main(int /*argc*/, char * /*argv*/[]) {
    qb::Main engine;
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. Please update it." << std::endl;
    }
    engine.addActor<ErrorHandlingActor>(0);
    engine.start();
    engine.join();
    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }
    qb::io::cout() << "Application finished." << std::endl;
    return 0;
}
