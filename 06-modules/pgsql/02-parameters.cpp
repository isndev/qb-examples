/**
 * @file examples/06-modules/pgsql/02-parameters.cpp
 * @tier 06-modules
 * @teaches Getting off string concatenation: prepare a statement once with its parameter OIDs, then
 *          execute it with qb::pg::params — including a NULL through std::optional.
 * @demonstrates qb::pg::tcp::database, qb::pg::params, qb::pg::type_oid_sequence, qb::pg::oid::int4,
 *               qb::pg::oid::text, qb::io::async::run_until, qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/01-connect-and-query
 * @expect "Successfully connected to PostgreSQL."
 * @expect "Database initialized and statements prepared successfully."
 * @example qbm-pgsql: Prepared Statements
 *
 * @brief Demonstrates prepared statement usage with the modern **coroutine** API:
 * create a table, prepare INSERT / SELECT statements, insert users, and select
 * them back by ID.
 * The example is standalone qb-io: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 *
 * Steps:
 * 1.  Connect via `co_await db.connect(uri)`.
 * 2.  Create the schema with `co_await db.execute(sql)`.
 * 3.  Prepare statements with `co_await db.prepare(name, sql, types)`.
 * 4.  Insert and select rows with `co_await db.execute(name, params{...})`.
 *
 * QB/QBM PostgreSQL features demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::pg::tcp::database` — the PostgreSQL client.
 * - `co_await db.prepare(name, sql, type_oid_sequence{})` → `Reply<PreparedQuery>`.
 * - `co_await db.execute(name, qb::pg::params{...})` → `Reply<resultset>`.
 * - `qb::pg::oid::*` — parameter type hints.
 * - `Reply<T>`: `ok()`, `result()`, `error()`.
 */

#include <qbm/pgsql/pgsql.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

#include <optional>
#include <string>
#include <vector>

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

// Prepared statement names
const char *PREPARE_INSERT_USER       = "insert_user_stmt_v2_5";
const char *PREPARE_SELECT_USER_BY_ID = "select_user_by_id_stmt_v2_5";

struct User {
    int                        id;
    std::string                name;
    std::optional<std::string> email;
};

// ─── Helper: insert a user then immediately select them back ───────────────────

qb::io::async::task<void>
insert_and_select_user(qb::pg::tcp::database &db, const std::string &name, std::optional<std::string> email) {
    qb::io::cout() << "Attempting to insert user: " << name << ", Email: " << (email ? *email : "NULL") << std::endl;

    // INSERT — params accept nullopt transparently.
    auto ins = co_await db.execute(PREPARE_INSERT_USER, qb::pg::params{name, email});
    if (!ins.ok()) {
        const auto &err = ins.error();
        qb::io::cerr() << "Failed to insert user '" << name << "': " << err.what() << std::endl;
        if (std::string(err.code) == "23505")
            qb::io::cerr() << "Detail: This might be due to a duplicate email." << std::endl;
        co_return;
    }
    const auto &ins_rs = ins.result();
    if (ins_rs.empty()) {
        qb::io::cerr() << "Insert user '" << name << "' did not return an ID." << std::endl;
        co_return;
    }
    int new_id = ins_rs[0]["id"].as<int>();
    qb::io::cout() << "Successfully inserted user '" << name << "' with ID: " << new_id << std::endl;

    // SELECT the newly created user back.
    auto sel = co_await db.execute(PREPARE_SELECT_USER_BY_ID, qb::pg::params{new_id});
    if (!sel.ok()) {
        qb::io::cerr() << "Failed to select user ID " << new_id << ": " << sel.error().what() << std::endl;
        co_return;
    }
    const auto &sel_rs = sel.result();
    if (sel_rs.empty()) {
        qb::io::cout() << "User with ID " << new_id << " not found." << std::endl;
        co_return;
    }
    const auto &row = sel_rs[0];
    User        user;
    user.id    = row["id"].as<int>();
    user.name  = row["name"].as<std::string>();
    user.email = row["email"].as<std::optional<std::string>>();
    qb::io::cout() << "Selected User ---- ID: " << user.id << ", Name: " << user.name << ", Email: " << (user.email ? *user.email : "N/A")
                   << " ----" << std::endl;
    co_return;
}

// ─── Main coroutine ───────────────────────────────────────────────────────────

// All PostgreSQL work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_prepared_statements(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;

    // 1. Connect.
    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        qb::io::cerr() << "Please ensure PostgreSQL is running and connection string is correct." << std::endl;
        co_return;
    }
    qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;

    // 2. Create schema.
    qb::io::cout() << "Initializing database schema and preparing statements..." << std::endl;
    {
        [[maybe_unused]] auto r = co_await db.execute("CREATE TABLE IF NOT EXISTS users ("
                                                      "id SERIAL PRIMARY KEY, "
                                                      "name TEXT NOT NULL, "
                                                      "email TEXT UNIQUE"
                                                      ");");
        if (!r.ok()) {
            qb::io::cerr() << "Failed to create users table: " << r.error().what() << std::endl;
            co_return;
        }
    }

    // 3. Prepare INSERT.
    {
        auto r = co_await db.prepare(PREPARE_INSERT_USER, "INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id;",
                                     qb::pg::type_oid_sequence{qb::pg::oid::text, qb::pg::oid::text});
        if (!r.ok()) {
            qb::io::cerr() << "Failed to prepare insert statement: " << r.error().what() << std::endl;
            co_return;
        }
    }

    // 4. Prepare SELECT.
    {
        auto r = co_await db.prepare(PREPARE_SELECT_USER_BY_ID, "SELECT id, name, email FROM users WHERE id = $1;",
                                     qb::pg::type_oid_sequence{qb::pg::oid::int4});
        if (!r.ok()) {
            qb::io::cerr() << "Failed to prepare select statement: " << r.error().what() << std::endl;
            co_return;
        }
    }
    qb::io::cout() << "Database initialized and statements prepared successfully." << std::endl;

    // 5. Application logic: insert several users and select them back.
    qb::io::cout() << "\nRunning application logic..." << std::endl;
    co_await insert_and_select_user(db, "Alice Wonderland", "alice@example.com");
    co_await insert_and_select_user(db, "Bob The Builder", std::nullopt);
    co_await insert_and_select_user(db, "Charlie Chaplin", "charlie@example.com");

    // Try selecting a non-existent user.
    {
        qb::io::cout() << "Attempting to select user with ID: 999" << std::endl;
        auto sel = co_await db.execute(PREPARE_SELECT_USER_BY_ID, qb::pg::params{999});
        if (!sel.ok()) {
            qb::io::cerr() << "Failed to select user with ID 999: " << sel.error().what() << std::endl;
        } else if (sel.result().empty()) {
            qb::io::cout() << "User with ID 999 not found." << std::endl;
        }
    }

    // 6. Cleanup.
    qb::io::cout() << "Cleaning up database (dropping users table)..." << std::endl;
    {
        [[maybe_unused]] auto r = co_await db.execute("DROP TABLE IF EXISTS users;");
        if (r.ok())
            qb::io::cout() << "Users table dropped successfully." << std::endl;
        else
            qb::io::cerr() << "Failed to drop users table: " << r.error().what() << std::endl;
    }

    qb::io::cout() << "Example operations complete." << std::endl;
    co_return;
}

int
main() {
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in 02-parameters.cpp with your actual database details." << std::endl;
    }

    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_prepared_statements(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    qb::io::cout() << "Application finished." << std::endl;
    return 0;
}
