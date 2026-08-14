/**
 * @file examples/06-modules/redis/05-transactions.cpp
 * @tier 06-modules
 * @teaches Redis transactions as this client actually implements them: MULTI queues, EXEC runs
 *          the batch in one atomic step, DISCARD throws it away, and WATCH makes the whole
 *          thing conditional on nobody else having touched a key. Plus the trap that decides
 *          whether your MULTI block works: what a QUEUED reply looks like to a TYPED client.
 * @demonstrates qb::redis::tcp::client, multi, exec<std::string>, discard, watch, unwatch,
 *               is_in_multi, qb::redis::Reply<T>, ok, result, raw, error,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/03-coroutines-and-pipelining
 * @expect "Connected to Redis successfully!"
 * @expect "[multi] queued 2 commands; is_in_multi() = yes, and each answered 'QUEUED' rather"
 * @expect "[exec] both commands ran atomically: OK OK, and the block is closed again"
 * @expect "[queued] SET (status)  -> ok=yes, value 'QUEUED' — the assertable shape"
 * @expect "[queued] INCR (long long) -> ok=no: a QUEUED reply is not an integer, so the typed"
 * @expect "[queued] GET (optional<string>) -> ok=yes and the value is literally \"QUEUED\". THIS is"
 * @expect "[discard] the queued SET never happened; the key still reads "
 * @expect "[watch] another client changed the key, so EXEC ABORTED"
 * @expect "[watch] raw()->is_null() = yes — THAT is how an abort is told from a parse error"
 * @expect "[watch] with nobody interfering, the guarded EXEC committed"
 * @expect "[raw] a heterogeneous EXEC read through raw(): string, integer, error — and one"
 * @expect "=== transactions complete: MULTI, EXEC, DISCARD, WATCH, UNWATCH all exercised ==="
 *
 * WHAT THIS FILE USED TO BE
 * -------------------------
 * It was titled *Transactions and Atomic Operations*, it was 716 lines of inventory actors, and
 * it called **no** MULTI, EXEC, WATCH or DISCARD — measured, zero call sites. It was the one
 * file in the restructured corpus whose filename was not true of it. The read-then-write it
 * demonstrated ("HGET the quantity, decide, HINCRBY it down") is exactly the race a transaction
 * exists to close, and it closed nothing.
 *
 * THE MODEL, IN FOUR SENTENCES
 * ----------------------------
 * `MULTI` opens a block. Every command you send afterwards is not executed — the server queues
 * it and answers `+QUEUED`. `EXEC` runs the whole queue as one atomic step and answers with an
 * ARRAY of the individual results, in order. `DISCARD` throws the queue away instead.
 *
 * `WATCH key` makes the next `EXEC` conditional: if any watched key was modified by anyone else
 * between the WATCH and the EXEC, the EXEC does nothing and returns nil. That is optimistic
 * concurrency control, and it is the *only* way to make a read-then-write atomic in Redis,
 * because you cannot read a value inside a MULTI block — the reply is not available until EXEC.
 *
 * THE TRAP: A TYPED CLIENT MEETS +QUEUED — three outcomes, and one is silent
 * --------------------------------------------------------------------------
 * This client is typed: `set()` resolves to `Reply<status>`, `incr()` to `Reply<long long>`,
 * `get()` to `Reply<std::optional<std::string>>`. Inside a MULTI block the server answers every
 * one of them with the simple string `+QUEUED`, and the typed parser reacts differently to that
 * depending on the type it expected. Measured against the parser, not guessed:
 *
 *   status                    ok() == true, result().str() == "QUEUED"    the assertable shape
 *   long long                 ok() == false, error() = "expect INTEGER reply, but got
 *                             SIMPLE_STRING reply"                        a false-looking failure
 *   std::optional<std::string> ok() == true, result() == "QUEUED"          a SILENT WRONG VALUE
 *
 * The last one is the dangerous one: `co_await redis.get(k)` inside a MULTI block hands you a
 * perfectly healthy-looking reply whose value is the string "QUEUED". Nothing in the API stops
 * you reading it. **The rule for a MULTI block is therefore: `co_await` every command — you must
 * consume its reply or the reply FIFO desynchronises permanently — but never read a value out of
 * one.** Read values from the EXEC array, which is where they actually are.
 *
 * READING THE EXEC ARRAY
 * ----------------------
 * `exec<T>()` has no default for `T` and it is not deducible, so `exec()` does not compile.
 * `co_await redis.exec<std::string>()` resolves to `Reply<std::vector<std::string>>`, which is
 * ONE reply containing a vector — not a vector of replies. That homogeneous form is convenient
 * and brittle: one element that is not a `T` makes the whole reply `ok() == false`. For a mixed
 * batch, read `raw()` and walk the RESP array with `is_string()` / `is_integer()` / `is_error()`,
 * which the last section does.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-transactions
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-transactions
 */

#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

#define REDIS_URI {"tcp://localhost:6379"}

namespace {

constexpr const char *K_A       = "qb:example:tx:a";
constexpr const char *K_B       = "qb:example:tx:b";
constexpr const char *K_GUARDED = "qb:example:tx:guarded";
constexpr const char *K_COUNTER = "qb:example:tx:counter";

} // namespace

qb::io::async::task<void>
run_transactions(bool &running) {
    // Flip `running` on EVERY exit path, so a failure stops the loop instead of hanging it.
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis\n";
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!\n\n";

    (void) co_await redis.del(K_A, K_B, K_GUARDED, K_COUNTER);

    // -----------------------------------------------------------------------------------
    // 1. MULTI / EXEC — the batch, and what the queued replies look like
    // -----------------------------------------------------------------------------------
    auto opened = co_await redis.multi();
    if (!opened.ok()) {
        qb::io::cerr() << "MULTI failed: " << opened.error() << "\n";
        co_return;
    }

    // Every queued command is still `co_await`ed. The reply must be consumed — this client
    // holds one FIFO of pending reply handlers, and skipping one desynchronises it for the rest
    // of the connection's life.
    auto q1 = co_await redis.set(K_A, "first");
    auto q2 = co_await redis.set(K_B, "second");
    // Each asserted sentence below is a WHOLE literal chosen by the measurement, never a value
    // spliced into one. That is what makes the example runner's `@expect` check an assertion
    // about behaviour instead of an assertion that this line was reached.
    const bool queued_ok = redis.is_in_multi() && q1.result().str() == "QUEUED" && q2.result().str() == "QUEUED";
    qb::io::cout() << (queued_ok ? "[multi] queued 2 commands; is_in_multi() = yes, and each answered 'QUEUED' rather\n"
                                   "        than a value — the server has not run them\n"
                                 : "[multi] UNEXPECTED: the queued replies were not QUEUED\n");

    // `exec<T>()`: T is not deducible and has no default, so `exec()` will not compile. The
    // result is ONE Reply holding a vector, indexed in the order the commands were queued.
    qb::redis::Reply<std::vector<std::string>> ran = co_await redis.exec<std::string>();
    if (!ran.ok()) {
        qb::io::cerr() << "EXEC failed: " << ran.error() << "\n";
        co_return;
    }
    const bool both = ran.result().size() == 2 && ran.result()[0] == "OK" && ran.result()[1] == "OK" && !redis.is_in_multi();
    qb::io::cout() << (both ? "[exec] both commands ran atomically: OK OK, and the block is closed again\n\n"
                            : "[exec] UNEXPECTED: the EXEC array was not two OKs\n\n");

    // -----------------------------------------------------------------------------------
    // 2. The three shapes a QUEUED reply takes, measured rather than described
    // -----------------------------------------------------------------------------------
    (void) co_await redis.set(K_COUNTER, "41");
    (void) co_await redis.multi();

    auto queued_set  = co_await redis.set(K_A, "ignored"); // Reply<status>
    auto queued_incr = co_await redis.incr(K_COUNTER);     // Reply<long long>
    auto queued_get  = co_await redis.get(K_A);            // Reply<std::optional<std::string>>

    qb::io::cout() << (queued_set.ok() && queued_set.result().str() == "QUEUED"
                           ? "[queued] SET (status)  -> ok=yes, value 'QUEUED' — the assertable shape\n"
                           : "[queued] SET did not produce the documented QUEUED status\n");
    qb::io::cout() << (!queued_incr.ok() ? "[queued] INCR (long long) -> ok=no: a QUEUED reply is not an integer, so the typed\n"
                                           "         parser reports a FAILURE for a command that was queued perfectly well\n"
                                         : "[queued] INCR reported ok, which the parser should not do inside MULTI\n");
    qb::io::cout() << "         (its error text was: " << queued_incr.error() << ")\n";
    qb::io::cout() << (queued_get.ok() && queued_get.result().has_value() && *queued_get.result() == "QUEUED"
                           ? "[queued] GET (optional<string>) -> ok=yes and the value is literally \"QUEUED\". THIS is\n"
                             "         the silent one: never read a value out of a queued reply\n"
                           : "[queued] GET did not silently return the QUEUED string\n");

    auto after = co_await redis.exec<std::string>();
    qb::io::cout() << "[queued] ...and after EXEC the real values are in the array: " << after.result().size()
                   << " results, the counter is now " << (co_await redis.get(K_COUNTER)).result().value_or("?") << "\n\n";

    // -----------------------------------------------------------------------------------
    // 3. DISCARD — the queue is thrown away, nothing ran
    // -----------------------------------------------------------------------------------
    (void) co_await redis.set(K_A, "unchanged");
    (void) co_await redis.multi();
    (void) co_await redis.set(K_A, "this must never land");
    auto dropped = co_await redis.discard();
    auto still   = co_await redis.get(K_A);
    qb::io::cout() << "[discard] the queued SET never happened; the key still reads '" << still.result().value_or("?") << "' (DISCARD "
                   << (dropped.ok() ? "ok" : "failed") << ", is_in_multi() " << (redis.is_in_multi() ? "yes" : "no") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 4. WATCH — the same batch, made conditional. First the ABORT.
    // -----------------------------------------------------------------------------------
    (void) co_await redis.set(K_GUARDED, "v1");
    (void) co_await redis.watch(K_GUARDED);

    // A SECOND connection: the interference has to come from somewhere else, or there is
    // nothing for WATCH to notice. This is what a competing process looks like.
    {
        qb::redis::tcp::client other{REDIS_URI};
        if (!co_await other.connect()) {
            qb::io::cerr() << "second client failed to connect\n";
            co_return;
        }
        (void) co_await other.set(K_GUARDED, "changed by somebody else");
    }

    (void) co_await redis.multi();
    (void) co_await redis.set(K_GUARDED, "v2");
    auto aborted = co_await redis.exec<std::string>();

    qb::io::cout() << "[watch] another client changed the key, so EXEC ABORTED and nothing was written: ok=" << (aborted.ok() ? "yes" : "no")
                   << ", key is still '" << (co_await redis.get(K_GUARDED)).result().value_or("?") << "'\n";

    // AND HOW TO TELL. An aborted EXEC answers RESP nil; the sequence parser then reports the
    // same `ok() == false` it would report for a genuine type mismatch, with a message about
    // NULL. `ok()` alone cannot distinguish "somebody else won the race" — a normal, retryable
    // outcome — from "your code asked for the wrong type". The raw reply can.
    qb::io::cout() << (aborted.raw() && aborted.raw()->is_null()
                           ? "[watch] raw()->is_null() = yes — THAT is how an abort is told from a parse error\n"
                           : "[watch] the aborted EXEC did not come back as a RESP nil\n");
    qb::io::cout() << "        (ok() alone says only 'no'; its text was: " << aborted.error() << ")\n";

    // ...and now the same guarded write with nobody interfering.
    (void) co_await redis.watch(K_GUARDED);
    (void) co_await redis.multi();
    (void) co_await redis.set(K_GUARDED, "v2");
    auto committed = co_await redis.exec<std::string>();
    qb::io::cout() << "[watch] with nobody interfering, the guarded EXEC committed: "
                   << (committed.ok() ? committed.result()[0] : committed.error()) << ", key is now '"
                   << (co_await redis.get(K_GUARDED)).result().value_or("?") << "'\n";

    // UNWATCH drops every watch this connection holds. EXEC and DISCARD also clear them, so
    // this matters on the path where you decide NOT to open a MULTI after all.
    auto unwatched = co_await redis.unwatch();
    qb::io::cout() << "[watch] UNWATCH releases every key this connection was watching: " << (unwatched.ok() ? "ok" : "failed") << "\n\n";

    // -----------------------------------------------------------------------------------
    // 5. A HETEROGENEOUS batch, read through raw()
    // -----------------------------------------------------------------------------------
    // `exec<std::string>()` would fail as a whole here, because one element is an integer and
    // one is an error. Walking the RESP array is the way to read a mixed batch — and note that
    // an error INSIDE the array does not abort the transaction: Redis runs the rest.
    (void) co_await redis.multi();
    (void) co_await redis.set(K_A, "text");
    (void) co_await redis.incr(K_COUNTER);
    (void) co_await redis.incr(K_A); // deliberate: INCR on a non-numeric string is an error
    auto mixed = co_await redis.exec<std::string>();

    if (mixed.raw() && mixed.raw()->is_array()) {
        auto const &arr   = mixed.raw()->as_array();
        const bool  shape = arr.size() == 3 && arr[0]->is_string() && arr[1]->is_integer() && arr[2]->is_error();
        qb::io::cout() << (shape ? "[raw] a heterogeneous EXEC read through raw(): string, integer, error — and one\n"
                                   "      failing command does NOT roll the others back; Redis has no rollback\n"
                                 : "[raw] the mixed EXEC array was not the expected string/integer/error shape\n");
        if (arr[2]->is_error())
            qb::io::cout() << "[raw] the third command's error was: " << arr[2]->get_error_message() << "\n";
    } else {
        qb::io::cerr() << "[raw] EXEC did not return an array\n";
    }

    (void) co_await redis.del(K_A, K_B, K_GUARDED, K_COUNTER);
    qb::io::cout() << "\n=== transactions complete: MULTI, EXEC, DISCARD, WATCH, UNWATCH all exercised ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    qb::io::async::coro_scheduler().spawn(run_transactions(running));
    qb::io::async::run_until(running);

    return 0;
}
