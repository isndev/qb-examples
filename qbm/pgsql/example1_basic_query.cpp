/**
 * @file examples/qbm/pgsql/example1_basic_query.cpp
 * @example qbm-pgsql: Basic Query
 *
 * @brief Demonstrates fundamental usage of the `qbm-pgsql` client: connecting to a
 * PostgreSQL server and running a simple query, using the modern **coroutine** API.
 * The example is standalone qb-io: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 *
 * Steps:
 * 1.  `run_basic_query()` coroutine — standalone entry point, driven by `run_until()`.
 * 2.  `co_await db.connect(uri)` — establish the connection (yields `bool`).
 * 3.  `co_await db.execute(sql)` — run a simple query, get `Reply<resultset>`.
 * 4.  Iterate the `resultset`, read fields with `.as<T>()`.
 *
 * QB/QBM PostgreSQL features demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::pg::tcp::database` — the PostgreSQL client.
 * - `co_await db.connect(uri)` — connect (yields `bool`).
 * - `co_await db.execute(sql)` — simple query (yields `Reply<resultset>`).
 * - `Reply<T>`: `ok()`, `result()`, `error()`.
 * - `qb::io::cout()` — thread-safe console output.
 */

#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <pgsql/pgsql.h>

#include <string>

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

// All PostgreSQL work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_basic_query(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;

    // Connect — co_await yields bool (true on success).
    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        qb::io::cerr() << "Please ensure PostgreSQL is running and the connection string is correct." << std::endl;
        co_return;
    }
    qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;

    // Run a simple query — returns Reply<resultset>.
    qb::io::cout() << "Executing query: SELECT version();" << std::endl;
    auto res = co_await db.execute("SELECT version();");
    if (!res.ok()) {
        qb::io::cerr() << "Query execution failed: " << res.error().what() << std::endl;
        co_return;
    }

    const auto &rs = res.result();
    if (!rs.empty()) {
        qb::io::cout() << "PostgreSQL Version: " << rs[0][0].as<std::string>() << std::endl;
    } else {
        qb::io::cout() << "Query executed, but no results returned." << std::endl;
    }

    qb::io::cout() << "Basic query completed successfully!" << std::endl;
    co_return;
}

int
main() {
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in example1_basic_query.cpp with your actual database details." << std::endl;
    }

    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_basic_query(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    qb::io::cout() << "Application finished successfully." << std::endl;
    return 0;
}
