/**
 * @file examples/06-modules/pgsql/10-streaming-results.cpp
 * @tier 06-modules
 * @teaches The result set that does not fit: `query()` buffers every row before you see the first
 *          one, `query_stream()` walks a server-side CURSOR and hands you one batch at a time. What
 *          the row handed to your callback actually IS (a view, valid only during the call), what
 *          happens to the cursor when your callback throws, and the rule that decides whether two
 *          overlapping streams on one connection work or destroy each other.
 * @demonstrates qb::pg::tcp::database, query_stream, query, execute, begin, commit, in_transaction,
 *               qb::pg::Reply<void>, qb::pg::results, ok, error,
 *               qb::io::async::when_all, qb::io::async::init, qb::io::async::run_until,
 *               qb::io::async::coro_scheduler, qb::io::async::task<void>,
 *               qb::io::async::task<std::uint64_t>
 * @prerequisites 06-modules/pgsql/06-typed-rows, 06-modules/pgsql/03-transactions
 * @expect "Connected — 20000 rows to read four different ways."
 * @expect "[buffered] query() returned every row at once: 20000 of them are resident before the"
 * @expect "[stream] query_stream() walked a server-side CURSOR instead: same 20000 rows, same"
 * @expect "[batch] the batch size is the memory knob and nothing else — the ANSWER does not depend"
 * @expect "[view] the row your callback receives is a VIEW into the batch buffer and is valid ONLY"
 * @expect "[throw] a callback that throws closes the cursor and RETHROWS, and the connection is"
 * @expect "[txn] a cursor needs a transaction. Inside one you opened, query_stream declares the"
 * @expect "[concurrent] two streams overlapping on ONE connection SHARE that self-opened block:"
 * @expect "[error] a bad statement is a FAILED Reply, not an exception — the exception path belongs"
 * @expect "=== streaming complete: the table is dropped, so a second run measures the same thing"
 *
 * THE PROBLEM
 * -----------
 * `co_await db.query("SELECT * FROM huge")` does exactly what it says: the client reads every
 * `DataRow` message the server sends, into memory, and only then resumes your coroutine. For a
 * thousand rows that is right. For ten million it is an out-of-memory kill, and no amount of
 * `LIMIT`-and-`OFFSET` paging fixes it properly — `OFFSET n` makes the server walk and discard n
 * rows every time, so a paged scan is quadratic.
 *
 * WHAT `query_stream` DOES INSTEAD
 * -------------------------------
 * It declares a server-side `CURSOR` over your statement and `FETCH`es `batch_size` rows at a
 * time, invoking your callback on each row AS THE BATCHES ARRIVE. Only one batch is ever held, so
 * the client's memory is `batch_size` rows regardless of how many the query matches, and the
 * server never materialises the whole set for you either. The cursor is closed on the way out —
 * on the error path and on the throwing path too.
 *
 * THE ROW IS A VIEW. THIS IS THE ONE THING TO GET RIGHT
 * ----------------------------------------------------
 * The `row` your callback receives points INTO the batch buffer, which is reused by the next
 * `FETCH`. It is valid for the duration of the call and not one instruction longer. Copy out what
 * you need — a value, a sum, a decoded object — and never store the row, a `field`, or a
 * `string_view` obtained from one. Section 3 does the copying explicitly so the shape is visible.
 *
 * CURSORS NEED A TRANSACTION, AND THAT HAS A CONSEQUENCE PEOPLE MEET THE HARD WAY
 * ------------------------------------------------------------------------------
 * PostgreSQL closes a non-holdable cursor at the end of the transaction, so `query_stream` needs
 * one. If you already opened a transaction it simply declares the cursor there and closes only the
 * cursor. If you did not, it opens its own `BEGIN` … `COMMIT`.
 *
 * And PostgreSQL allows exactly ONE transaction per session. So two `query_stream` calls
 * overlapping on the same connection cannot each have a block: the first opens it, the last to
 * finish ends it, and it is rolled back if either failed. That also means a *server* error in one
 * of them aborts the transaction for the other. Section 6 runs two at once and checks both answers;
 * the reason it is worth checking is that this is precisely where a fixed cursor NAME would make
 * both streams return zero rows, each having closed the other's cursor.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-streaming-results
 * Run (needs a PostgreSQL on 127.0.0.1:5432 with test:test@[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-streaming-results
 */

#include <cstdint>
#include <exception>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>

namespace {

const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

constexpr const char   *TABLE = "qb_example_streaming";
constexpr std::uint64_t ROWS  = 20000;

// A named coroutine, not a lambda: `spawn([&]{...}())` calls a temporary closure whose lifetime
// ends at the semicolon, leaving the frame referencing freed memory. The same rule applies to
// anything handed to `when_all`, which is why section 6 uses this instead of two inline lambdas.
qb::io::async::task<std::uint64_t>
stream_sum(qb::pg::tcp::database &db, std::string sql, std::size_t batch) {
    std::uint64_t total = 0;
    auto          done = co_await db.query_stream(std::move(sql), batch, [&total](auto row) { total += row["v"].template as<std::int64_t>(); });
    if (!done.ok())
        co_return 0;
    co_return total;
}

} // namespace

qb::io::async::task<void>
run_streaming(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;
    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        co_return;
    }

    // ---- setup ------------------------------------------------------------------------
    (void) co_await db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";");
    // generate_series builds the whole table SERVER-SIDE in one statement, so the setup is one
    // round trip rather than 20000.
    auto created = co_await db.execute(std::string("CREATE TABLE ") + TABLE + " AS SELECT g AS v, 'row-' || g AS label FROM generate_series(1, "
                                       + std::to_string(ROWS) + ") AS g;");
    if (!created.ok()) {
        qb::io::cerr() << "CREATE TABLE failed: " << created.error().what() << std::endl;
        co_return;
    }
    qb::io::cout() << "Connected — 20000 rows to read four different ways.\n"
                      "(the sum of 1..20000 is 200010000, which is the invariant every section below must reproduce)\n\n";

    const std::uint64_t expected_sum = ROWS * (ROWS + 1) / 2;
    const std::string   select_all   = std::string("SELECT v FROM ") + TABLE + " ORDER BY v";

    // -----------------------------------------------------------------------------------
    // 1. THE BASELINE — query() buffers everything.
    // -----------------------------------------------------------------------------------
    std::uint64_t buffered_sum  = 0;
    std::size_t   buffered_rows = 0;
    {
        auto all = co_await db.query(select_all + ";");
        // Spelled out once rather than `auto`: `qb::pg::results` is the whole set, held by the
        // client, and that is exactly the object this section is warning about.
        [[maybe_unused]] qb::pg::results const &everything = all.result();
        if (!all.ok()) {
            qb::io::cerr() << "[buffered] UNEXPECTED: " << all.error().what() << "\n";
            (void) co_await db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";");
            co_return;
        }
        // Every row is already here. `size()` is not a stream position, it is a count of what the
        // client is holding — which for a big table is the whole problem.
        buffered_rows = everything.size();
        for (auto const &row : everything)
            buffered_sum += static_cast<std::uint64_t>(row["v"].as<std::int64_t>());
    }

    const bool buffered_ok = buffered_rows == ROWS && buffered_sum == expected_sum;

    qb::io::cout() << "[buffered] query() returned every row at once: 20000 of them are resident before the\n"
                      "           loop below sees the first, because the awaiter does not resume until the last\n"
                      "           DataRow has been read. Right for a thousand rows, an OOM kill for ten million\n";
    qb::io::cout() << "           (" << buffered_rows << " rows held at once, summing to " << buffered_sum << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2. THE SAME ANSWER, ONE BATCH AT A TIME.
    // -----------------------------------------------------------------------------------
    std::uint64_t streamed_sum  = 0;
    std::uint64_t streamed_rows = 0;
    // `query_stream` yields Reply<void> — there is no result SET to hand back, because the rows
    // were delivered to the callback as they arrived. Spelled out for exactly that reason.
    qb::pg::Reply<void> streamed = co_await db.query_stream(select_all, 1000, [&](auto row) {
        streamed_sum += static_cast<std::uint64_t>(row["v"].template as<std::int64_t>());
        ++streamed_rows;
    });

    const bool stream_ok = streamed.ok() && streamed_rows == ROWS && streamed_sum == expected_sum;

    qb::io::cout() << "[stream] query_stream() walked a server-side CURSOR instead: same 20000 rows, same\n"
                      "         answer, but the client never held more than one batch of 1000. The callback runs\n"
                      "         AS THE BATCHES ARRIVE, so work overlaps the next FETCH rather than following it\n";
    qb::io::cout() << "         (" << streamed_rows << " rows seen, summing to " << streamed_sum << " — identical to the buffered read)\n\n";

    // -----------------------------------------------------------------------------------
    // 3. THE KNOB, AND WHAT IT DOES NOT CHANGE.
    // -----------------------------------------------------------------------------------
    std::uint64_t tiny_batch_rows = 0;
    std::uint64_t tiny_batch_sum  = 0;
    auto          tiny            = co_await db.query_stream(select_all, 1, [&](auto row) {
        tiny_batch_sum += static_cast<std::uint64_t>(row["v"].template as<std::int64_t>());
        ++tiny_batch_rows;
    });

    std::uint64_t huge_batch_rows = 0;
    std::uint64_t huge_batch_sum  = 0;
    auto          huge            = co_await db.query_stream(select_all, 50000, [&](auto row) {
        huge_batch_sum += static_cast<std::uint64_t>(row["v"].template as<std::int64_t>());
        ++huge_batch_rows;
    });

    const bool batch_ok = tiny.ok() && huge.ok() && tiny_batch_sum == expected_sum && huge_batch_sum == expected_sum && tiny_batch_rows == ROWS
                          && huge_batch_rows == ROWS;

    qb::io::cout() << "[batch] the batch size is the memory knob and nothing else — the ANSWER does not depend\n"
                      "        on it. 1 row per FETCH is 20000 round trips and one row of memory; 50000 is one\n"
                      "        round trip and the whole table, which is `query()` again with extra steps. A\n"
                      "        batch of 0 is clamped to 1 rather than dividing by zero\n";
    qb::io::cout() << "        (batch=1: " << tiny_batch_rows << " rows, sum " << tiny_batch_sum << "; batch=50000: " << huge_batch_rows
                   << " rows, sum " << huge_batch_sum << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 4. THE ROW IS A VIEW.
    // -----------------------------------------------------------------------------------
    // Copying out is not defensive style, it is the contract. Everything kept below is a VALUE.
    std::vector<std::string> first_three_labels;
    std::uint64_t            scanned = 0;
    auto viewed = co_await db.query_stream(std::string("SELECT v, label FROM ") + TABLE + " ORDER BY v", 500, [&](auto row) {
        ++scanned;
        if (first_three_labels.size() < 3)
            // as<std::string> COPIES. `field::text()` would hand
            // back a string_view into the batch buffer, which the
            // next FETCH overwrites.
            first_three_labels.push_back(row["label"].template as<std::string>());
    });

    const bool view_ok = viewed.ok() && scanned == ROWS && first_three_labels.size() == 3 && first_three_labels[0] == "row-1";

    qb::io::cout() << "[view] the row your callback receives is a VIEW into the batch buffer and is valid ONLY\n"
                      "       during the call — the next FETCH overwrites those bytes. Copy out a value (as<T>\n"
                      "       copies); never keep the row, a field, or a string_view taken from one\n";
    qb::io::cout() << "       (kept by value from a stream of " << scanned << ": ";
    for (std::size_t i = 0; i < first_three_labels.size(); ++i)
        qb::io::cout() << (i ? ", " : "") << first_three_labels[i];
    qb::io::cout() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. A CALLBACK THAT THROWS.
    // -----------------------------------------------------------------------------------
    // The cursor is closed and the exception is rethrown, so the caller sees its own exception and
    // the connection is left usable. This is the shape a "stop early on a bad record" scan takes.
    bool          caught      = false;
    std::string   what        = "unexpected";
    std::uint64_t before_stop = 0;
    try {
        (void) co_await db.query_stream(select_all, 250, [&](auto row) {
            ++before_stop;
            if (row["v"].template as<std::int64_t>() == 777)
                throw std::runtime_error("stopping at row 777");
        });
    } catch (std::exception const &e) {
        caught = true;
        what   = e.what();
    }

    // The proof that the cursor really was closed: the connection still works.
    auto       after_throw = co_await db.query(std::string("SELECT count(*) AS n FROM ") + TABLE + ";");
    const bool throw_ok    = caught && what == "stopping at row 777" && before_stop == 777 && after_throw.ok()
                             && after_throw.result()[0]["n"].as<std::int64_t>() == static_cast<std::int64_t>(ROWS);

    qb::io::cout() << "[throw] a callback that throws closes the cursor and RETHROWS, and the connection is\n"
                      "        left usable — which is what makes 'stop at the first bad record' a normal thing\n"
                      "        to write rather than a leak. The rows already delivered stay delivered\n";
    qb::io::cout() << "        (threw at row " << before_stop << " with \"" << what << "\"; the next query on the same connection answered "
                   << (after_throw.ok() ? after_throw.result()[0]["n"].as<std::int64_t>() : -1) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 6. THE TRANSACTION RULES.
    // -----------------------------------------------------------------------------------
    // (a) Inside a transaction the caller opened, only the cursor is closed.
    std::uint64_t inside_rows = 0;
    bool          inside_txn  = false;
    {
        auto began = co_await db.begin();
        inside_txn = began.ok() && db.in_transaction();
        auto in_tx = co_await db.query_stream(select_all, 2000, [&](auto) { ++inside_rows; });
        (void) in_tx;
        (void) co_await db.commit();
    }
    const bool txn_ok = inside_txn && inside_rows == ROWS && !db.in_transaction();

    qb::io::cout() << "[txn] a cursor needs a transaction. Inside one you opened, query_stream declares the\n"
                      "      cursor there and closes only the cursor — your block is still yours. Outside one,\n"
                      "      it opens its own BEGIN and COMMITs it (or ROLLBACKs, if the stream failed)\n";
    qb::io::cout() << "      (streamed " << inside_rows << " rows inside a caller-opened transaction; afterwards in_transaction() = "
                   << (db.in_transaction() ? "true — UNEXPECTED" : "false") << ")\n\n";

    // (b) Two streams overlapping on ONE connection.
    auto                both          = co_await qb::io::async::when_all(stream_sum(db, select_all, 700), stream_sum(db, select_all, 300));
    const std::uint64_t first_sum     = std::get<0>(both);
    const std::uint64_t second_sum    = std::get<1>(both);
    const bool          concurrent_ok = first_sum == expected_sum && second_sum == expected_sum;

    qb::io::cout() << "[concurrent] two streams overlapping on ONE connection SHARE that self-opened block:\n"
                      "             PostgreSQL allows one transaction per session, so the first to start opens it\n"
                      "             and the last to finish ends it. Each gets its own CURSOR NAME, which is what\n"
                      "             stops them from closing each other's — without that they would BOTH read zero\n";
    qb::io::cout() << "             (batch 700 summed " << first_sum << ", batch 300 summed " << second_sum
                   << ", and both match the buffered read)\n";
    qb::io::cout() << "             (the cost of sharing: a SERVER error in one of them aborts the block for the\n"
                      "             other, so overlapping streams succeed together or fail together)\n\n";

    // -----------------------------------------------------------------------------------
    // 7. FAILURE IS A REPLY, NOT AN EXCEPTION.
    // -----------------------------------------------------------------------------------
    std::uint64_t never = 0;
    auto          bad   = co_await db.query_stream("SELECT no_such_column FROM no_such_table", 100, [&](auto) { ++never; });

    const bool error_ok = !bad.ok() && never == 0 && !bad.error().code.empty();

    qb::io::cout() << "[error] a bad statement is a FAILED Reply, not an exception — the exception path belongs\n"
                      "        to YOUR callback and nothing else. So a stream is checked the same way every other\n"
                      "        operation in this client is: ok() first, then the value\n";
    qb::io::cout() << "        (SQLSTATE " << bad.error().code << ", callback invoked " << never << " times)\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    auto dropped = co_await db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";");

    ok = buffered_ok && stream_ok && batch_ok && view_ok && throw_ok && txn_ok && concurrent_ok && error_ok && dropped.ok();

    qb::io::cout() << "=== streaming complete: the table is dropped, so a second run measures the same thing\n"
                      "    as the first, and every section above reproduced the same sum from a different\n"
                      "    amount of memory ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_streaming(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
