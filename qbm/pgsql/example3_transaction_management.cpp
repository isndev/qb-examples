/**
 * @file examples/qbm/pgsql/example3_transaction_management.cpp
 * @example qbm-pgsql: Transaction Management
 *
 * @brief Demonstrates transaction control with the modern **coroutine** API:
 * fund transfers (BEGIN / execute / COMMIT / ROLLBACK), savepoints, and rollback
 * on intentional duplicate-key errors.
 * The example is standalone qb-io: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 *
 * Steps:
 * 1.  Connect via `co_await db.connect(uri)`.
 * 2.  Set up schema and prepared statements.
 * 3.  Execute a successful fund transfer inside a manual BEGIN/COMMIT.
 * 4.  Execute a failing transfer (duplicate insert of 'Eve') and ROLLBACK.
 * 5.  Verify final balances, then clean up.
 *
 * QB/QBM PostgreSQL features demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `co_await db.begin()` / `co_await db.commit()` / `co_await db.rollback()`.
 * - `co_await db.execute(name, qb::pg::params{...})` → `Reply<resultset>`.
 * - `co_await db.prepare(name, sql, types)` → `Reply<PreparedQuery>`.
 */

#include <pgsql/pgsql.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

// Table for this example — using DOUBLE PRECISION for balance
const char *ACCOUNTS_TABLE_SQL = "CREATE TABLE IF NOT EXISTS accounts ("
                                 "id SERIAL PRIMARY KEY, "
                                 "name TEXT NOT NULL UNIQUE, "
                                 "balance DOUBLE PRECISION NOT NULL DEFAULT 0.0"
                                 ");";

// Prepared statement names
const char *PREPARE_INSERT_ACCOUNT         = "insert_account_stmt_v3_5";
const char *PREPARE_UPDATE_BALANCE         = "update_balance_stmt_v3_5";
const char *PREPARE_SELECT_ACCOUNT_BY_NAME = "select_account_by_name_stmt_v3_5";
const char *PREPARE_DELETE_ACCOUNT         = "delete_account_stmt_v3_5";

// Helper: format double with 2 decimal places
std::string
format_decimal(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

// ─── Schema setup ─────────────────────────────────────────────────────────────

qb::io::async::task<bool>
setup_schema(qb::pg::tcp::database &db) {
    qb::io::cout() << "Initializing accounts table and preparing statements..." << std::endl;

    {
        auto r = co_await db.execute(ACCOUNTS_TABLE_SQL);
        if (!r.ok()) {
            qb::io::cerr() << "Failed to create accounts table: " << r.error().what() << std::endl;
            co_return false;
        }
    }

    const struct {
        const char               *name;
        const char               *sql;
        qb::pg::type_oid_sequence types;
    } stmts[] = {
        {PREPARE_INSERT_ACCOUNT,
         "INSERT INTO accounts (name, balance) VALUES ($1, $2) RETURNING id, name, balance;",
         {qb::pg::oid::text, qb::pg::oid::float8}},
        {PREPARE_UPDATE_BALANCE, "UPDATE accounts SET balance = balance + $1 WHERE name = $2;", {qb::pg::oid::float8, qb::pg::oid::text}},
        {PREPARE_SELECT_ACCOUNT_BY_NAME, "SELECT id, name, balance FROM accounts WHERE name = $1;", {qb::pg::oid::text}},
        {PREPARE_DELETE_ACCOUNT, "DELETE FROM accounts WHERE name = $1;", {qb::pg::oid::text}},
    };

    for (auto &s : stmts) {
        auto r = co_await db.prepare(s.name, s.sql, qb::pg::type_oid_sequence{s.types});
        if (!r.ok()) {
            qb::io::cerr() << "Failed to prepare '" << s.name << "': " << r.error().what() << std::endl;
            co_return false;
        }
    }

    qb::io::cout() << "Schema and statements initialized." << std::endl;
    co_return true;
}

// ─── Ensure an account exists (insert or tolerate duplicate-key) ──────────────

qb::io::async::task<void>
ensure_account(qb::pg::tcp::database &db, const std::string &name, double initial_balance) {
    qb::io::cout() << "Ensuring account: " << name << " with balance: " << initial_balance << std::endl;
    auto r = co_await db.execute(PREPARE_INSERT_ACCOUNT, qb::pg::params{name, initial_balance});
    if (r.ok()) {
        if (!r.result().empty())
            qb::io::cout() << "Ensured account (created): '" << name << "' with balance " << r.result()[0]["balance"].as<double>() << std::endl;
        else
            qb::io::cout() << "Ensured account (created): '" << name << "'" << std::endl;
    } else {
        if (std::string(r.error().code) == "23505")
            qb::io::cout() << "Ensured account (already existed): '" << name << "'" << std::endl;
        else
            qb::io::cerr() << "Error ensuring account '" << name << "': " << r.error().what() << std::endl;
    }
    co_return;
}

// ─── Display account ──────────────────────────────────────────────────────────

qb::io::async::task<void>
display_account(qb::pg::tcp::database &db, const std::string &name) {
    auto r = co_await db.execute(PREPARE_SELECT_ACCOUNT_BY_NAME, qb::pg::params{name});
    if (!r.ok()) {
        qb::io::cerr() << "Error selecting account '" << name << "': " << r.error().what() << std::endl;
        co_return;
    }
    const auto &rs = r.result();
    if (!rs.empty()) {
        qb::io::cout() << "Account '" << name << "': ID=" << rs[0]["id"].as<int>() << ", Balance=" << rs[0]["balance"].as<double>()
                       << std::endl;
    } else {
        qb::io::cout() << "Account '" << name << "' not found." << std::endl;
    }
    co_return;
}

// ─── Successful fund transfer ─────────────────────────────────────────────────

qb::io::async::task<void>
transfer_funds(qb::pg::tcp::database &db, const std::string &from_account, const std::string &to_account, double amount) {
    qb::io::cout() << "\nAttempting to transfer " << amount << " from '" << from_account << "' to '" << to_account << "'." << std::endl;

    // Ensure both accounts exist before starting the transaction.
    co_await ensure_account(db, from_account, 100.00);
    co_await ensure_account(db, to_account, 20.00);

    // BEGIN.
    {
        auto r = co_await db.begin();
        if (!r.ok()) {
            qb::io::cerr() << "FATAL: Could not begin transaction for transfer " << from_account << " -> " << to_account << ": "
                           << r.error().what() << std::endl;
            co_return;
        }
    }
    qb::io::cout() << "Transaction started for transferring funds (" << from_account << " -> " << to_account << ")." << std::endl;

    // Debit from_account.
    qb::io::cout() << "Debiting " << amount << " from '" << from_account << "'" << std::endl;
    {
        auto r = co_await db.execute(PREPARE_UPDATE_BALANCE, qb::pg::params{-amount, from_account});
        if (!r.ok()) {
            qb::io::cerr() << "Debit failed: " << r.error().what() << std::endl;
            [[maybe_unused]] auto rb = co_await db.rollback();
            co_return;
        }
    }

    // Credit to_account.
    qb::io::cout() << "Crediting " << amount << " to '" << to_account << "'" << std::endl;
    {
        auto r = co_await db.execute(PREPARE_UPDATE_BALANCE, qb::pg::params{amount, to_account});
        if (!r.ok()) {
            qb::io::cerr() << "Credit failed: " << r.error().what() << std::endl;
            [[maybe_unused]] auto rb = co_await db.rollback();
            co_return;
        }
    }

    // COMMIT.
    {
        auto r = co_await db.commit();
        if (!r.ok()) {
            qb::io::cerr() << "ERROR: Transaction commit failed: " << r.error().what() << std::endl;
            co_return;
        }
    }
    qb::io::cout() << "SUCCESS: Transaction for transferring " << amount << " from '" << from_account << "' to '" << to_account
                   << "' committed." << std::endl;
    co_return;
}

// ─── Failing transaction (duplicate 'Eve') ────────────────────────────────────

qb::io::async::task<void>
transfer_funds_with_error(qb::pg::tcp::database &db) {
    qb::io::cout() << "\nAttempting transaction that is expected to fail (duplicate insert of 'Eve')..." << std::endl;

    // Ensure 'Eve' exists first.
    co_await ensure_account(db, "Eve", 100.0);

    // Begin.
    {
        auto r = co_await db.begin();
        if (!r.ok()) {
            qb::io::cerr() << "FATAL: Could not begin transaction for 'Eve' error scenario: " << r.error().what() << std::endl;
            co_return;
        }
    }

    // Attempt to insert 'Eve' again — should fail with unique_violation.
    qb::io::cout() << "Attempting to insert duplicate account 'Eve' to trigger error..." << std::endl;
    auto r = co_await db.execute(PREPARE_INSERT_ACCOUNT, qb::pg::params{std::string("Eve"), 50.0});
    if (!r.ok()) {
        qb::io::cout() << "EXPECTED FAILURE: Transaction (duplicate 'Eve') rolled back as intended." << std::endl;
        qb::io::cout() << "Reason: " << r.error().what() << std::endl;
        qb::io::cout() << "SQLSTATE: " << r.error().code << std::endl;
        [[maybe_unused]] auto rb = co_await db.rollback();
        co_return;
    }

    // Should not reach here.
    [[maybe_unused]] auto cm = co_await db.commit();
    qb::io::cerr() << "UNEXPECTED SUCCESS: Transaction with intentional error committed." << std::endl;
    co_return;
}

// ─── Main coroutine ───────────────────────────────────────────────────────────

// All PostgreSQL work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_transaction_demo(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;

    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        qb::io::cerr() << "Please ensure PostgreSQL is running and connection string is correct." << std::endl;
        co_return;
    }
    qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;

    if (!co_await setup_schema(db))
        co_return;

    qb::io::cout() << "\n--- Running Transaction Scenarios ---" << std::endl;

    // Scenario 1: Successful transfer.
    co_await transfer_funds(db, "Alice", "Bob", 50.00);

    // Scenario 2: Failed transfer (duplicate key).
    co_await transfer_funds_with_error(db);

    // Scenario 3: Verify balances.
    qb::io::cout() << "\n--- Verifying Account Balances Post-Transactions ---" << std::endl;
    for (const auto &name : {"Alice", "Bob", "Charlie", "David", "Eve", "ErrorTriggerAccount"})
        co_await display_account(db, name);

    // Cleanup.
    qb::io::cout() << "Cleaning up accounts table..." << std::endl;
    {
        [[maybe_unused]] auto r = co_await db.execute("DROP TABLE IF EXISTS accounts;");
        if (r.ok())
            qb::io::cout() << "Accounts table dropped." << std::endl;
        else
            qb::io::cerr() << "Failed to drop accounts table: " << r.error().what() << std::endl;
    }

    qb::io::cout() << "Example operations and checks complete." << std::endl;
    co_return;
}

int
main() {
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in example3_transaction_management.cpp with your actual database details." << std::endl;
    }

    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_transaction_demo(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    qb::io::cout() << "Application finished." << std::endl;
    return 0;
}
