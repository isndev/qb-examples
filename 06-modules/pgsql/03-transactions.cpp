/**
 * @file examples/06-modules/pgsql/03-transactions.cpp
 * @tier 06-modules
 * @teaches Everything the word "transaction" covers in this client: the manual BEGIN/COMMIT you
 *          start with, `with_transaction` which deletes the branch you keep forgetting, SAVEPOINTs
 *          for partial rollback, an isolation/read-only mode, and a per-transaction statement
 *          timeout — with the one rule that makes all of them work.
 * @demonstrates qb::pg::tcp::database, qb::pg::with_transaction, qb::pg::transaction_abort,
 *               qb::pg::transaction_mode, qb::pg::isolation_level, savepoint,
 *               rollback_savepoint, release_savepoint, set_timeout, begin, commit, rollback,
 *               query, execute, qb::pg::sqlstate::query_canceled, qb::io::async::init,
 *               qb::io::async::run_until, qb::io::async::coro_scheduler, qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/02-parameters
 * @expect "Successfully connected to PostgreSQL."
 * @expect "[manual] BEGIN / UPDATE / UPDATE / COMMIT — Alice 50, Bob 70"
 * @expect "[with_transaction] the happy path returned a value and committed: total 120"
 * @expect "[with_transaction] transaction_abort rolled back and came back as a FAILED Reply"
 * @expect "[with_transaction] a non-abort exception also rolls back, then RETHROWS"
 * @expect "[with_transaction] nesting is refused before any BEGIN is sent"
 * @expect "[savepoint] Carol committed, Dave was rolled back to the savepoint, Erin committed"
 * @expect "[mode] a READ ONLY serializable transaction refused the write, as asked"
 * @expect "[timeout] set_timeout made a slow statement fail with SQLSTATE 57014"
 * @expect "=== transactions complete: manual, with_transaction, savepoints, mode, timeout ==="
 *
 * WHAT THIS FILE USED TO BE
 * -------------------------
 * Its `@brief` promised "savepoints" and its body contained none — measured, zero calls to
 * `savepoint`, `rollback_savepoint` or `release_savepoint`. It also had no `with_transaction`,
 * and it finished by checking the balances of three accounts it never created, printing
 * "Account 'Charlie' not found." as though that were a result. All three are fixed here.
 *
 * THE ONE RULE
 * ------------
 * **A failed statement does not abort anything by itself.** `execute` and `query` return a
 * `Reply` and never throw, so a transaction whose second UPDATE failed will happily COMMIT the
 * first one unless you look. Every block below therefore checks `.ok()` after every statement.
 * Inside `with_transaction` the way to say "stop" is `throw qb::pg::transaction_abort{r.error()}`.
 *
 * THE THREE SHAPES, IN THE ORDER YOU WILL MEET THEM
 * -------------------------------------------------
 * 1. **Manual.** `co_await db.begin()`, statements, `co_await db.commit()` — and a
 *    `co_await db.rollback()` on every failure branch. It is correct and it is the one people
 *    get wrong, because "every failure branch" includes the ones added later.
 * 2. **`qb::pg::with_transaction(db, body)`.** The BEGIN, the COMMIT and the ROLLBACK move into
 *    the library; `body` is `f(Transaction&) -> task<T>` and its value comes back inside a
 *    `Reply<T>`. Its exception contract is exact and worth memorising:
 *      * `transaction_abort`  -> ROLLBACK, and a FAILED `Reply` carrying your error. Swallowed.
 *      * any other exception  -> ROLLBACK, then **rethrown**.
 *      * COMMIT itself failed -> ROLLBACK, and a failed `Reply`.
 *      * already in a transaction -> refused immediately, with no BEGIN sent at all.
 * 3. **SAVEPOINTs.** PostgreSQL has one transaction per session; a second BEGIN does not nest,
 *    it flattens — which is why `with_transaction` refuses rather than allowing it. Partial
 *    rollback is spelled `savepoint(name)` / `rollback_savepoint(name)` / `release_savepoint(name)`.
 *    Note those two names: it is NOT `rollback_to` and NOT `release`.
 *
 * TWO KNOBS
 * ---------
 * `transaction_mode` is a STRUCT, not an enum — `{isolation_level, read_only, deferrable}` — and
 * it is passed to `begin(mode)` or to the three-argument `with_transaction(db, mode, body)`.
 * `set_timeout(d)` emits `SET LOCAL statement_timeout` in the same round trip as the BEGIN, so it
 * must be called BEFORE `begin()`; it is sticky, and `qb::duration::zero()` clears it.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-transactions
 * Run (needs a PostgreSQL at tcp://test:test@localhost:5432[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-transactions
 */

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>

namespace {

const char *PG_URI = "tcp://test:test@localhost:5432[test]";

const char *SCHEMA_SQL = "CREATE TABLE IF NOT EXISTS qb_tx_accounts ("
                         "id SERIAL PRIMARY KEY, "
                         "name TEXT NOT NULL UNIQUE, "
                         "balance BIGINT NOT NULL DEFAULT 0);";

/// Read one balance, or `std::nullopt` when the row is not there.
qb::io::async::task<std::optional<std::int64_t>>
balance_of(qb::pg::tcp::database &db, std::string name) {
    auto r = co_await db.query("SELECT balance FROM qb_tx_accounts WHERE name = $1", name);
    if (!r.ok() || r.result().empty())
        co_return std::nullopt;
    co_return r.result().front()[0].as<std::int64_t>();
}

} // namespace

qb::io::async::task<void>
run_transactions(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;
    if (!co_await db.connect(PG_URI)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL at " << PG_URI << "\n";
        co_return;
    }
    qb::io::cout() << "Successfully connected to PostgreSQL.\n\n";

    (void) co_await db.execute("DROP TABLE IF EXISTS qb_tx_accounts;");
    if (!(co_await db.execute(SCHEMA_SQL)).ok()) {
        qb::io::cerr() << "Failed to create the accounts table\n";
        co_return;
    }
    (void) co_await db.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2), ($3, $4)", "Alice", std::int64_t{100}, "Bob",
                             std::int64_t{20});

    // -----------------------------------------------------------------------------------
    // 1. MANUAL — begin, statements, commit, and a rollback on every failure branch
    // -----------------------------------------------------------------------------------
    {
        bool ok = (co_await db.begin()).ok();
        if (ok) {
            auto debit = co_await db.query("UPDATE qb_tx_accounts SET balance = balance - $1 WHERE name = $2", std::int64_t{50}, "Alice");
            // The rule: a failed statement does NOT abort. Without this check the COMMIT below
            // would happily commit a transfer that only debited or only credited.
            if (!debit.ok()) {
                (void) co_await db.rollback();
                ok = false;
            }
        }
        if (ok) {
            auto credit = co_await db.query("UPDATE qb_tx_accounts SET balance = balance + $1 WHERE name = $2", std::int64_t{50}, "Bob");
            if (!credit.ok()) {
                (void) co_await db.rollback();
                ok = false;
            }
        }
        if (ok)
            ok = (co_await db.commit()).ok();

        const auto a = co_await balance_of(db, "Alice");
        const auto b = co_await balance_of(db, "Bob");
        qb::io::cout() << (ok && a == std::int64_t{50} && b == std::int64_t{70}
                               ? "[manual] BEGIN / UPDATE / UPDATE / COMMIT — Alice 50, Bob 70, and the two\n"
                                 "         rollback branches above are what this shape costs you to write\n"
                               : "[manual] UNEXPECTED: the manual transfer did not land\n");
    }

    // -----------------------------------------------------------------------------------
    // 2. with_transaction — the same work with the branches deleted
    // -----------------------------------------------------------------------------------
    {
        // The body is `f(Transaction&) -> task<T>`; T comes back inside a Reply<T>. Note the
        // body takes the transaction it is given and does NOT touch `db` — the two are the same
        // connection here, but writing against the parameter is what makes a body reusable.
        auto res = co_await qb::pg::with_transaction(db, [](qb::pg::detail::Transaction &tr) -> qb::io::async::task<std::int64_t> {
            auto d = co_await tr.query("UPDATE qb_tx_accounts SET balance = balance - $1 WHERE name = $2", std::int64_t{10}, "Alice");
            if (!d.ok())
                throw qb::pg::transaction_abort{d.error()};
            auto c = co_await tr.query("UPDATE qb_tx_accounts SET balance = balance + $1 WHERE name = $2", std::int64_t{10}, "Bob");
            if (!c.ok())
                throw qb::pg::transaction_abort{c.error()};
            auto t = co_await tr.query("SELECT SUM(balance) FROM qb_tx_accounts");
            co_return t.ok() && !t.result().empty() ? t.result().front()[0].as<std::int64_t>() : std::int64_t{-1};
        });
        qb::io::cout() << (res.ok() && res.result() == std::int64_t{120}
                               ? "[with_transaction] the happy path returned a value and committed: total 120\n"
                               : "[with_transaction] UNEXPECTED: the happy path did not commit or did not return 120\n");
    }

    // ...and the abort path. A duplicate name violates the UNIQUE constraint, the body turns
    // that into `transaction_abort`, and the whole block is rolled back — including the write
    // that succeeded before it.
    {
        auto       res     = co_await qb::pg::with_transaction(db, [](qb::pg::detail::Transaction &tr) -> qb::io::async::task<int> {
            auto ins = co_await tr.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Mallory", std::int64_t{5});
            if (!ins.ok())
                throw qb::pg::transaction_abort{ins.error()};
            auto dup = co_await tr.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Alice", std::int64_t{1});
            if (!dup.ok())
                throw qb::pg::transaction_abort{dup.error()}; // <- fires: name is UNIQUE
            co_return 1;
        });
        const auto mallory = co_await balance_of(db, "Mallory");
        qb::io::cout() << (!res.ok() && !mallory.has_value()
                               ? "[with_transaction] transaction_abort rolled back and came back as a FAILED Reply —\n"
                                 "                   the insert that SUCCEEDED before it was rolled back too\n"
                               : "[with_transaction] UNEXPECTED: the abort did not roll everything back\n");
        if (!res.ok())
            qb::io::cout() << "                   (its error was: " << res.error().what() << ")\n";
    }

    // A non-`transaction_abort` exception rolls back as well — and is then RETHROWN, so it is
    // your problem again. That asymmetry is the whole reason `transaction_abort` exists.
    {
        bool rethrown = false;
        try {
            (void) co_await qb::pg::with_transaction(db, [](qb::pg::detail::Transaction &tr) -> qb::io::async::task<int> {
                (void) co_await tr.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Trent", std::int64_t{7});
                throw std::runtime_error{"a bug in the body, not a database error"};
            });
        } catch (std::runtime_error const &) {
            rethrown = true;
        }
        const auto trent = co_await balance_of(db, "Trent");
        qb::io::cout() << (rethrown && !trent.has_value()
                               ? "[with_transaction] a non-abort exception also rolls back, then RETHROWS: the row is\n"
                                 "                   gone and the exception reached us\n"
                               : "[with_transaction] UNEXPECTED: the plain exception was swallowed or the row survived\n");
    }

    // Nesting is refused BEFORE any BEGIN is sent, because PostgreSQL has one transaction per
    // session and a second BEGIN flattens rather than nests — the inner COMMIT would end the
    // outer transaction and everything after it would run unprotected.
    {
        (void) co_await db.begin();
        auto nested = co_await qb::pg::with_transaction(db, [](qb::pg::detail::Transaction &) -> qb::io::async::task<int> { co_return 1; });
        (void) co_await db.rollback();
        qb::io::cout() << (!nested.ok() ? "[with_transaction] nesting is refused before any BEGIN is sent — use a SAVEPOINT\n"
                                        : "[with_transaction] UNEXPECTED: a nested transaction was accepted\n");
    }

    // -----------------------------------------------------------------------------------
    // 3. SAVEPOINTS — partial rollback inside ONE transaction
    // -----------------------------------------------------------------------------------
    {
        bool ok = (co_await db.begin()).ok();
        ok      = ok && (co_await db.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Carol", std::int64_t{30})).ok();

        // A named point to come back to. Names are validated (non-empty, <= 63 bytes,
        // [A-Za-z0-9_]) and quoted, so they are case-sensitive and injection-safe.
        ok = ok && (co_await db.savepoint("after_carol")).ok();

        // This one fails: UNIQUE on `name`. Everything since the savepoint is undone, and the
        // transaction stays usable — without the savepoint the failed statement would have
        // poisoned the whole block (25P02, "current transaction is aborted").
        auto bad = co_await db.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Alice", std::int64_t{1});
        if (!bad.ok())
            ok = ok && (co_await db.rollback_savepoint("after_carol")).ok();

        ok = ok && (co_await db.query("INSERT INTO qb_tx_accounts (name, balance) VALUES ($1, $2)", "Erin", std::int64_t{40})).ok();
        // RELEASE forgets the point (it does not undo anything). Skipping it is legal; the
        // savepoint simply lives to the end of the transaction.
        ok = ok && (co_await db.release_savepoint("after_carol")).ok();
        ok = ok && (co_await db.commit()).ok();

        const auto carol = co_await balance_of(db, "Carol");
        const auto dave  = co_await balance_of(db, "Dave"); // never inserted; the check the old file got wrong
        const auto erin  = co_await balance_of(db, "Erin");
        qb::io::cout() << (ok && carol == std::int64_t{30} && !dave.has_value() && erin == std::int64_t{40}
                               ? "[savepoint] Carol committed, Dave was rolled back to the savepoint, Erin committed —\n"
                                 "            one transaction, a partial undo, and the block stayed usable\n"
                               : "[savepoint] UNEXPECTED: the savepoint round trip did not land as described\n");
    }

    // -----------------------------------------------------------------------------------
    // 4. transaction_mode — isolation and READ ONLY
    // -----------------------------------------------------------------------------------
    {
        // A STRUCT, and its constructor is explicit: {isolation, read_only, deferrable}.
        const qb::pg::transaction_mode ro{qb::pg::isolation_level::serializable, /*read_only*/ true};

        auto       res = co_await qb::pg::with_transaction(db, ro, [](qb::pg::detail::Transaction &tr) -> qb::io::async::task<int> {
            auto w = co_await tr.query("UPDATE qb_tx_accounts SET balance = balance + $1 WHERE name = $2", std::int64_t{1}, "Alice");
            if (!w.ok())
                throw qb::pg::transaction_abort{w.error()}; // 25006, read-only transaction
            co_return 1;
        });
        const auto a   = co_await balance_of(db, "Alice");
        qb::io::cout() << (!res.ok() && a == std::int64_t{40} ? "[mode] a READ ONLY serializable transaction refused the write, as asked\n"
                                                              : "[mode] UNEXPECTED: the read-only transaction accepted a write\n");
    }

    // -----------------------------------------------------------------------------------
    // 5. set_timeout — a bound on a statement, not on the socket
    // -----------------------------------------------------------------------------------
    {
        // It emits `SET LOCAL statement_timeout` alongside the BEGIN, so it must be set BEFORE
        // begin() and it applies inside the transaction only. It is also STICKY: every later
        // begin() re-emits it until it is cleared.
        db.set_timeout(std::chrono::milliseconds{200});
        auto res = co_await qb::pg::with_transaction(db, [](qb::pg::detail::Transaction &tr) -> qb::io::async::task<int> {
            auto slow = co_await tr.execute("SELECT pg_sleep(2);");
            if (!slow.ok())
                throw qb::pg::transaction_abort{slow.error()};
            co_return 1;
        });
        // `db_error` carries BOTH spellings: `code` is the five-character string PostgreSQL
        // sent, `sqlstate` is the structured enum. Compare against the enum.
        const bool cancelled = !res.ok() && res.error().sqlstate == qb::pg::sqlstate::query_canceled;
        qb::io::cout() << (cancelled ? "[timeout] set_timeout made a slow statement fail with SQLSTATE 57014 —\n"
                                       "          statement_timeout, not a socket timeout\n"
                                     : "[timeout] UNEXPECTED: the 2 s statement was not cut off at 200 ms\n");
        if (!res.ok())
            qb::io::cout() << "          (message: " << res.error().what() << ")\n";
        db.set_timeout(qb::duration::zero()); // sticky — clear it, or every later BEGIN carries it
    }

    (void) co_await db.execute("DROP TABLE IF EXISTS qb_tx_accounts;");
    qb::io::cout() << "\n=== transactions complete: manual, with_transaction, savepoints, mode, timeout ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    qb::io::async::coro_scheduler().spawn(run_transactions(running));
    qb::io::async::run_until(running);

    qb::io::cout() << "Application finished.\n";
    return 0;
}
