/**
 * @file examples/06-modules/pgsql/09-callbacks-and-await.cpp
 * @tier 06-modules
 * @teaches qbm-pgsql from code that is NOT a coroutine — which is the half of this client the
 *          other eight programs never show. The fluent chain (execute/then/success/error), the
 *          three `discard_*` handlers that exist so a callback is never simply omitted,
 *          `prepare_file()` for SQL that lives in a .sql file, and `await()`, the one blocking
 *          drain that turns a queue of callbacks back into straight-line code.
 * @demonstrates qb::pg::tcp::database, qb::pg::await, qb::pg::discard_query, qb::pg::discard_error,
 *               qb::pg::discard_prepare, qb::pg::params, qb::pg::type_oid_sequence,
 *               qb::pg::oid::float8, qb::pg::oid::int4, qb::pg::error::db_error,
 *               execute, prepare, prepare_file, then, success, error, await,
 *               qb::io::async::init, qb::io::async::run_sync
 * @prerequisites 06-modules/pgsql/02-parameters, 06-modules/pgsql/05-errors
 * @expect "[connect] connect() is the ONE call with no callback form, so run_sync() is the bridge:"
 * @expect "[chain] execute(sql, on_success, on_error) returns the Transaction, so the calls CHAIN."
 * @expect "[chain] and nothing above had run when the chain was built — await() is what executed"
 * @expect "[discard] discard_query / discard_error / discard_prepare are not decoration: the"
 * @expect "[error] a failing statement runs the .error() node and SKIPS the .then() node, and the"
 * @expect "[file] prepare_file() reads the statement from resources/sql/top-scores.sql, so the SQL"
 * @expect "[params] the prepared statement executed with BOUND parameters: 3 rows at or above"
 * @expect "[status] await() returns a SNAPSHOT — results, error and a bool — and resets the"
 * @expect "[rule] one style per call stack: in a callback chain use callback overloads and"
 * @expect "=== callbacks and await complete: the table is dropped, so a second run measures the"
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * `qbm-pgsql` has two complete APIs over one connection. Every operation is declared twice:
 *
 *     [[nodiscard]] pg_reply_awaiter<resultset> execute(std::string_view);            // co_await
 *     Transaction &execute(std::string_view, CB_SUCCESS &&, CB_ERROR &&);             // callback
 *
 * The coroutine half is what the rest of this group demonstrates. This file is the other half, and
 * it is not a legacy path — it is what you use from a `main()`, a migration tool, a test fixture,
 * a thread that is not running an event loop, or any function that cannot be made a coroutine.
 * There is not one `co_await` below.
 *
 * THE CHAIN IS A QUEUE, NOT A SEQUENCE OF CALLS
 * ---------------------------------------------
 * `execute(...)` returns `Transaction&`, so `.then()`, `.success()` and `.error()` chain off it.
 * None of them runs anything. Each one appends a node to a work queue: `then`/`success` append a
 * node whose callback fires **if the previous step succeeded**, `error` one that fires **if it
 * failed**. `await()` then turns the event loop until that queue is empty, running each node's
 * callback as it drains. So the whole chain is built synchronously, costs nothing, and executes at
 * exactly one point — which is what makes the batch a PIPELINE rather than N round trips.
 *
 * WHY `discard_query` EXISTS AT ALL
 * ---------------------------------
 * The callback overloads take the handler by value and call it unconditionally — there is no
 * per-reply `if (cb)` branch on the hot path. So a handler is never absent, and "I do not care
 * about this result" needs a real callable to say so. `qb::pg::discard_query`,
 * `qb::pg::discard_error` and `qb::pg::discard_prepare` are those callables: three
 * `constexpr` no-ops with the exact signatures the three operations expect. Reaching for
 * `nullptr` instead is a compile error, which is the correct outcome.
 *
 * `await()` MAY BE CALLED FROM A COROUTINE, AND STILL MUST NOT BE HERE
 * -------------------------------------------------------------------
 * `Transaction::await()` drives libev through `listener::current.run(EVRUN_ONCE)` rather than
 * `qb::io::async::run_once()`, precisely so it can be called from inside a coroutine body without
 * tripping the re-entrancy assert. That is a deliberate accommodation, not an invitation: mixing
 * the two styles on one call stack means some of your work is driven by the coroutine scheduler
 * and some by this drain, and a discarded awaiter in a callback chain is never driven by anything.
 * Pick one style per call stack. This file picks callbacks, and the section at the end says so.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-callbacks-and-await
 * Run (needs a PostgreSQL on 127.0.0.1:5432 with test:test@[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-callbacks-and-await
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>

namespace {

const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

constexpr const char *TABLE    = "qb_example_callbacks";
constexpr const char *STMT     = "qb_example_top_scores";
constexpr const char *SQL_FILE = "resources/sql/top-scores.sql";

} // namespace

int
main() {
    qb::io::async::init();

    qb::pg::tcp::database db;

    // -----------------------------------------------------------------------------------
    // 0. CONNECT — the one operation with no callback form.
    // -----------------------------------------------------------------------------------
    // `connect()` returns an awaiter and nothing else, so synchronous code needs one bridge.
    // `qb::io::async::run_sync` is it: it pumps the loop until the awaitable completes and hands
    // back its value. This is legal here because main() is not inside a coroutine — calling it
    // from one is refused by an assert, on purpose.
    if (!qb::io::async::run_sync(db.connect(PG_CONNECTION_STRING))) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        return 1;
    }
    qb::io::cout() << "[connect] connect() is the ONE call with no callback form, so run_sync() is the bridge:\n"
                      "          it pumps the loop until the handshake completes and returns its value. Every\n"
                      "          other operation below has a callback overload and needs no such thing\n\n";

    // -----------------------------------------------------------------------------------
    // 1. THE CHAIN — built synchronously, executed by await().
    // -----------------------------------------------------------------------------------
    // A previous run that died mid-way must not change what this one measures.
    db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";", qb::pg::discard_query, qb::pg::discard_error);

    bool        created_fired = false;
    bool        seeded_fired  = false;
    bool        chain_error   = false;
    std::size_t seeded_rows   = 0;

    db.execute(
          std::string("CREATE TABLE ") + TABLE + " (id SERIAL PRIMARY KEY, name TEXT NOT NULL, score DOUBLE PRECISION NOT NULL);",
          // on_success takes the Transaction and the result set. A DDL statement returns no
          // rows, so this one only records that it ran.
          [&created_fired](qb::pg::detail::Transaction &, qb::pg::results) { created_fired = true; },
          [&chain_error](qb::pg::error::db_error const &) { chain_error = true; })
        .execute(
            std::string("INSERT INTO ") + TABLE + " (name, score) VALUES ('ada', 91.5), ('grace', 88.0), ('alan', 95.25), ('edsger', 79.75);",
            // A plain INSERT returns no ROWS — only a command tag — so this result set is
            // empty by design. Add `RETURNING id` and it stops being.
            [&seeded_rows](qb::pg::detail::Transaction &, qb::pg::results rs) { seeded_rows = rs.size(); }, qb::pg::discard_error)
        // `.then()` and `.success()` are the same node under two names — literally: both push a
        // `Then` node. Use whichever reads better at the call site; there is no behavioural
        // difference to learn.
        .success([&seeded_fired](qb::pg::detail::Transaction &) { seeded_fired = true; })
        // `.error()` fires only if it did NOT. Nothing here should reach it.
        .error([&chain_error](qb::pg::error::db_error const &) { chain_error = true; });

    const bool nothing_ran = !created_fired && !seeded_fired;

    // THE DRAIN.
    auto setup = db.await();

    qb::io::cout() << "[chain] execute(sql, on_success, on_error) returns the Transaction, so the calls CHAIN.\n"
                      "        .then() / .success() append a node that fires only if the step before it\n"
                      "        SUCCEEDED; .error() appends one that fires only if it failed\n";
    qb::io::cout() << "[chain] and nothing above had run when the chain was built — await() is what executed\n"
                      "        it, all of it, in one pass. That is why a batch of statements costs one round\n"
                      "        trip's worth of waiting instead of one per statement\n";
    qb::io::cout() << "        (handlers that had fired before await(): " << (nothing_ran ? "none" : "some — UNEXPECTED")
                   << "; after it: CREATE=" << (created_fired ? "ran" : "did not run") << ", the INSERT's own result set held " << seeded_rows
                   << " row(s) because it has no RETURNING clause"
                   << ", .then()=" << (seeded_fired ? "ran" : "did not run")
                   << ", .error()=" << (chain_error ? "ran — UNEXPECTED" : "did not run") << ")\n\n";

    const bool chain_ok = nothing_ran && created_fired && seeded_fired && !chain_error && static_cast<bool>(setup);

    qb::io::cout() << "[discard] discard_query / discard_error / discard_prepare are not decoration: the\n"
                      "          callback overloads take the handler by value and call it unconditionally, with\n"
                      "          no per-reply branch, so a handler is never ABSENT. 'I do not care about this\n"
                      "          one' therefore needs a real callable, and those three constexpr no-ops are it\n";
    qb::io::cout() << "          (the DROP above and the INSERT's error arm used discard_error; the inline prepare below\n"
                      "          uses discard_prepare, whose signature is (Transaction&, PreparedQuery const&) and not\n"
                      "          the one execute() wants — which is why there are three and not one)\n\n";

    // -----------------------------------------------------------------------------------
    // 2. FAILURE, and which node runs.
    // -----------------------------------------------------------------------------------
    bool        then_after_failure = false;
    bool        error_fired        = false;
    std::string failure_state;

    db.execute("SELECT * FROM a_table_that_does_not_exist;", qb::pg::discard_query, [&](qb::pg::error::db_error const &e) {
          error_fired   = true;
          failure_state = e.code;
      }).then([&then_after_failure](qb::pg::detail::Transaction &) { then_after_failure = true; });

    auto failed = db.await();

    // 42P01 is undefined_table. The status is false and carries the same error the callback saw.
    const bool error_ok = error_fired && !then_after_failure && !static_cast<bool>(failed) && failure_state == "42P01";

    qb::io::cout() << "[error] a failing statement runs the .error() node and SKIPS the .then() node, and the\n"
                      "        status await() hands back is false as well — so a chain can be checked in two\n"
                      "        places, at the node that knows the detail and at the drain that knows the outcome\n";
    qb::io::cout() << "        (SQLSTATE " << failure_state << " = undefined_table; .error() " << (error_fired ? "ran" : "did NOT run")
                   << ", .then() " << (then_after_failure ? "also ran — UNEXPECTED" : "did not run")
                   << ", status = " << (static_cast<bool>(failed) ? "true — UNEXPECTED" : "false") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2b. THE THIRD DISCARD, on the operation that needs it.
    // -----------------------------------------------------------------------------------
    // `prepare()` takes a DIFFERENT success signature from `execute()` — (Transaction&,
    // PreparedQuery const&) rather than (Transaction&, results) — so it needs its own no-op, and
    // that is the whole reason there are three of them rather than one.
    db.prepare("qb_example_count", std::string("SELECT count(*) FROM ") + TABLE + ";", qb::pg::type_oid_sequence{}, qb::pg::discard_prepare,
               qb::pg::discard_error);
    const bool inline_prepared = static_cast<bool>(db.await());

    // -----------------------------------------------------------------------------------
    // 3. SQL THAT LIVES IN A .sql FILE.
    // -----------------------------------------------------------------------------------
    // The runner starts every example in its own binary directory and the build stages
    // resources/ there, so a plain relative path resolves.
    bool        prepared     = false;
    bool        prepare_fail = false;
    std::string prepare_error;

    if (!std::filesystem::exists(SQL_FILE)) {
        qb::io::cerr() << "[file] UNEXPECTED: " << SQL_FILE << " is not next to this binary — resource staging failed\n";
    } else {
        db.prepare_file(
            STMT, std::filesystem::path(SQL_FILE),
            // The parameter OIDs, in order: $1 is a float8 minimum score, $2 an int4 limit.
            qb::pg::type_oid_sequence{qb::pg::oid::float8, qb::pg::oid::int4},
            [&prepared](qb::pg::detail::Transaction &, qb::pg::detail::PreparedQuery const &) { prepared = true; },
            [&](qb::pg::error::db_error const &e) {
                prepare_fail  = true;
                prepare_error = e.what();
            });
        (void) db.await();
    }

    qb::io::cout() << "[file] prepare_file() reads the statement from resources/sql/top-scores.sql, so the SQL\n"
                      "       is reviewable, diffable and lintable AS SQL instead of surviving as an escaped C++\n"
                      "       literal. The parameter OIDs still come from the call site, because the file says\n"
                      "       nothing about types\n";
    qb::io::cout() << "       (PREPARE " << STMT << ": " << (prepared ? "succeeded" : "FAILED") << (prepare_fail ? " — " : "") << prepare_error
                   << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 4. EXECUTING IT, with bound parameters.
    // -----------------------------------------------------------------------------------
    std::vector<std::pair<std::string, double>> top;
    bool                                        query_error = false;

    db.execute(
        STMT, qb::pg::params{85.0, 3},
        [&top](qb::pg::detail::Transaction &, qb::pg::results rs) {
            for (auto const &row : rs)
                top.emplace_back(row["name"].as<std::string>(), row["score"].as<double>());
        },
        [&query_error](qb::pg::error::db_error const &) { query_error = true; });

    auto ran = db.await();

    const bool params_ok = prepared && !query_error && top.size() == 3 && static_cast<bool>(ran);

    qb::io::cout() << "[params] the prepared statement executed with BOUND parameters: 3 rows at or above\n"
                      "         score 85, newest-first, LIMITed by the second one. `qb::pg::params{}` sends the\n"
                      "         values separately from the statement text, so there is no string to escape and\n"
                      "         no injection to build by accident\n";
    qb::io::cout() << "         (";
    for (std::size_t i = 0; i < top.size(); ++i)
        qb::io::cout() << (i ? ", " : "") << top[i].first << " " << top[i].second;
    qb::io::cout() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. THE STATUS SNAPSHOT, and the free function.
    // -----------------------------------------------------------------------------------
    // `qb::pg::await(db)` is exactly `db.await()`, spelled as a free function so a helper can take
    // a `Transaction&` without knowing the concrete client type.
    db.execute(std::string("SELECT count(*) AS n FROM ") + TABLE + ";", qb::pg::discard_query, qb::pg::discard_error);
    auto counted = qb::pg::await(db);

    std::int64_t rows_in_table = -1;
    {
        // `status::results()` is the whole result set the batch produced, kept in the snapshot —
        // so the value survives the drain even though nothing captured it in a callback.
        auto rs = counted.results();
        if (!rs.empty())
            rows_in_table = rs[0]["n"].as<std::int64_t>();
    }

    // A second await() with an empty queue reports success and an untouched error — the snapshot
    // resets the transaction to a neutral baseline on its way out, so chains do not inherit each
    // other's outcome.
    auto empty_drain = db.await();

    const bool status_ok = static_cast<bool>(counted) && counted() && rows_in_table == 4 && static_cast<bool>(empty_drain);

    qb::io::cout() << "[status] await() returns a SNAPSHOT — results, error and a bool — and resets the\n"
                      "         transaction to a neutral baseline on the way out, so the next chain does not\n"
                      "         inherit this one's outcome. qb::pg::await(db) is the same call as a free\n"
                      "         function, for helpers that take a Transaction& and not a concrete client\n";
    qb::io::cout() << "         (the batch's own result set survived the drain: " << rows_in_table
                   << " rows in the table; a second await() over an empty queue reports "
                   << (static_cast<bool>(empty_drain) ? "success" : "FAILURE — UNEXPECTED") << ")\n\n";

    qb::io::cout() << "[rule] one style per call stack: in a callback chain use callback overloads and\n"
                      "       await(); in a coroutine use the awaiter overloads and co_await. A discarded\n"
                      "       awaiter inside a callback chain is driven by nothing at all, and the symptom is\n"
                      "       a statement that silently never runs rather than an error you can read\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    bool dropped = false;
    db.execute(
        std::string("DROP TABLE IF EXISTS ") + TABLE + ";", [&dropped](qb::pg::detail::Transaction &, qb::pg::results) { dropped = true; },
        qb::pg::discard_error);
    auto cleaned = db.await();

    const bool ok = chain_ok && inline_prepared && error_ok && params_ok && status_ok && dropped && static_cast<bool>(cleaned);

    qb::io::cout() << "=== callbacks and await complete: the table is dropped, so a second run measures the\n"
                      "    same thing as the first, and not one co_await was used to get here ===\n";

    return ok ? 0 : 1;
}
