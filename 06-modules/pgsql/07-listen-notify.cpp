/**
 * @file examples/06-modules/pgsql/07-listen-notify.cpp
 * @tier 06-modules
 * @teaches PostgreSQL as an event bus: LISTEN/NOTIFY, a `notify_co_consumer` you `co_await`, a
 *          TRIGGER that publishes on every INSERT — and the four rules that decide whether that
 *          is a good idea (transactional delivery, no durability, an 8000-byte payload, and a
 *          subscription that does not survive a reconnect).
 * @demonstrates qb::pg::tcp::database, qb::pg::tcp::notify_co_consumer, qb::pg::notification,
 *               listen, unlisten, unlisten_all, notify, on_notify, on_notify_dropped, receive,
 *               notify_channel_capacity, begin, commit, rollback, execute, is_connected,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::sleep, qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/03-transactions
 * @expect "Connected: one publisher, one notify_co_consumer, one channel."
 * @expect "[basic] LISTEN then NOTIFY: the consumer woke on co_await receive() with the channel,"
 * @expect "[txn] a NOTIFY inside a transaction is delivered at COMMIT and never at all if the"
 * @expect "[trigger] the INSERT itself published: a trigger called pg_notify(), so the writer did"
 * @expect "[rule] NOTIFY is NOT a queue — one sent while nobody was listening is gone, and no"
 * @expect "[rule] the subscription belongs to the CONNECTION: after a reconnect you must LISTEN"
 * @expect "[reuse] a reconnected consumer's co_await receive() comes back: the drop closed its"
 * @expect "=== listen/notify complete: the trigger, the function and the table are dropped ==="
 *
 * WHAT THIS IS FOR
 * ----------------
 * A cache that must be invalidated when a row changes, a worker that should wake when a job is
 * queued, a dashboard that should refresh — all of them are usually written as a poll: `SELECT ...
 * WHERE updated_at > $1` every second, forever, per instance. LISTEN/NOTIFY replaces that with a
 * push over the connection you already have, and PostgreSQL will do it from inside a TRIGGER, so
 * the application doing the write does not have to know anyone is watching.
 *
 * FOUR RULES, AND YOU NEED ALL FOUR BEFORE YOU SHIP THIS
 * ------------------------------------------------------
 * 1. DELIVERY IS TRANSACTIONAL. A NOTIFY inside a transaction is queued and sent at COMMIT; a
 *    rollback throws it away. That is a feature — you cannot announce a row that was never
 *    written — and it is also why a NOTIFY does not arrive when you expected it to.
 * 2. IT IS NOT A QUEUE. A notification sent while nobody is listening is dropped by the server,
 *    with no record of it anywhere. Anything that must survive a restart needs a table.
 * 3. THE PAYLOAD IS SMALL — 8000 bytes, enforced client-side here by `build_notify_sql`. Send an
 *    IDENTIFIER and let the reader fetch the row; a payload that carries the data is a payload
 *    that will one day be too big.
 * 4. THE SUBSCRIPTION BELONGS TO THE CONNECTION. Reconnect and you are subscribed to nothing —
 *    which is exactly the moment you are most likely to have missed something. Re-LISTEN and then
 *    reconcile against the table.
 *
 * TWO WAYS TO READ, ON ONE OBJECT
 * -------------------------------
 * `notify_co_consumer` delivers to BOTH an optional `on_notify(...)` callback and an internal
 * channel you `co_await receive()`. The callback runs on the I/O thread the instant the frame is
 * parsed, which is the right place to count or to filter; `receive()` is the right place to do
 * work, because it runs in your coroutine and can `co_await` in turn. The channel is BOUNDED
 * (`notify_channel_capacity()`), and `on_notify_dropped(...)` is how you find out you fell behind
 * instead of quietly losing events.
 *
 * A LIFETIME NOTE THAT IS EASY TO GET WRONG THE OTHER WAY
 * -------------------------------------------------------
 * `receive()` parks on the consumer's queue, and both a disconnect and `~notify_co_consumer` CLOSE
 * that queue — so unlike every other module awaiter here, a parked `receive()` does resume when
 * the link drops or the consumer is destroyed, with `std::nullopt`. That makes
 * `while (auto n = co_await sub.receive())` a loop that ends by itself, rather than an orphaned
 * frame; a reconnected consumer serves its next connection from a fresh queue (section 5).
 *
 * Everything this program creates — one table, one function, one trigger — is dropped on the way
 * out, and the consumer UNLISTENs.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-listen-notify
 * Run (needs a PostgreSQL on 127.0.0.1:5432 with test:test@[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-listen-notify
 */

#include <chrono>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include "example-database.h"
#include <qbm/pgsql/pgsql.h>

using namespace std::chrono_literals;

namespace {

const char *PG_CONNECTION_STRING = example_pg_connection_string();

std::string           CHANNEL;
constexpr const char *TABLE   = "qb_example_notify_jobs";
constexpr const char *FUNC    = "pg_temp.qb_example_notify_job";
constexpr const char *TRIGGER = "qb_example_notify_job_trg";

} // namespace

qb::io::async::task<void>
run_listen_notify(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    // Two connections, and it has to be two: a NOTIFY is delivered to every LISTENing session
    // including the one that sent it, but a publisher and a subscriber are separate roles and
    // separate sockets in anything real.
    qb::pg::tcp::database           pub;
    qb::pg::tcp::notify_co_consumer sub;

    if (!co_await pub.connect(PG_CONNECTION_STRING) || !co_await sub.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << pub.error().what() << std::endl;
        co_return;
    }

    // A backend PID is unique among live sessions, so concurrent runs never share a channel.
    auto pid = co_await pub.execute("SELECT pg_backend_pid();");
    if (!pid.ok() || pid.result().empty()) {
        qb::io::cerr() << "Could not obtain publisher backend PID\n";
        co_return;
    }
    CHANNEL = "qb_example_events_" + std::to_string(pid.result()[0][0].as<int>());

    // The callback half. It runs on the I/O thread as the frame is parsed — before any coroutine
    // resumes — which is what makes it the right place for a counter and the wrong place for work.
    int seen_via_callback = 0;
    sub.on_notify([&seen_via_callback](qb::pg::notification &&) { ++seen_via_callback; });
    // The drop handler is backpressure: it fires when the consumer's queue is full — you are
    // behind. A disconnect closes the queue too, but no notification arrives on a closed link, and
    // the next connection is served by a fresh queue (section 5).
    sub.on_notify_dropped([](qb::pg::notification &&n) {
        qb::io::cout() << "        [dropped] a notification on '" << n.channel
                       << "' did not reach receive(): the consumer's\n"
                          "                  queue is full\n";
    });

    if (!(co_await sub.listen(CHANNEL)).ok()) {
        qb::io::cerr() << "LISTEN failed\n";
        co_return;
    }
    qb::io::cout() << "Connected: one publisher, one notify_co_consumer, one channel.\n"
                   << "(the consumer's channel holds up to " << sub.notify_channel_capacity()
                   << " notifications before it starts dropping)\n\n";

    // -----------------------------------------------------------------------------------
    // 1. THE BASIC EXCHANGE
    // -----------------------------------------------------------------------------------
    if (!(co_await pub.notify(CHANNEL, "job:1001")).ok()) {
        qb::io::cerr() << "NOTIFY failed\n";
        co_return;
    }

    // Parked until the frame arrives. Nothing else on this thread is blocked while it waits.
    std::optional<qb::pg::notification> first = co_await sub.receive();

    const bool basic_ok = first.has_value() && first->channel == CHANNEL && first->payload == "job:1001" && first->server_backend_pid > 0;
    qb::io::cout() << (basic_ok ? "[basic] LISTEN then NOTIFY: the consumer woke on co_await receive() with the channel,\n"
                                  "        the payload and the backend PID that sent it — no polling, no timer, and the\n"
                                  "        same connection you were already holding\n"
                                : "[basic] UNEXPECTED: the first notification did not arrive intact\n");
    qb::io::cout() << "        (channel '" << (first ? first->channel : "?") << "', payload '" << (first ? first->payload : "?")
                   << "', from backend pid " << (first ? first->server_backend_pid : 0) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2. DELIVERY IS TRANSACTIONAL
    // -----------------------------------------------------------------------------------
    // The negative half is measured with the CALLBACK, not with receive(): "nothing arrived" is
    // an assertion about a window of time, and `receive()` would simply park for ever waiting for
    // the thing that must not come.
    const int before_rollback = seen_via_callback;
    (void) co_await pub.begin();
    (void) co_await pub.notify(CHANNEL, "this-must-never-arrive");
    co_await qb::io::async::sleep(150ms); // a generous window for a delivery that must not happen
    const int during_txn = seen_via_callback;
    (void) co_await pub.rollback();
    co_await qb::io::async::sleep(150ms);
    const int after_rollback = seen_via_callback;

    (void) co_await pub.begin();
    (void) co_await pub.notify(CHANNEL, "job:1002");
    (void) co_await pub.commit();
    std::optional<qb::pg::notification> committed = co_await sub.receive();

    const bool txn_ok =
        during_txn == before_rollback && after_rollback == before_rollback && committed.has_value() && committed->payload == "job:1002";
    qb::io::cout() << (txn_ok ? "[txn] a NOTIFY inside a transaction is delivered at COMMIT and never at all if the\n"
                                "      transaction rolls back — so you cannot announce a row that was not written, and\n"
                                "      a notification that 'did not arrive' is often a transaction still open\n"
                              : "[txn] UNEXPECTED: the rolled-back NOTIFY was delivered, or the committed one was not\n");
    qb::io::cout() << "      (callback count across the rolled-back window: " << before_rollback << " -> " << after_rollback
                   << "; the committed one arrived as '" << (committed ? committed->payload : "?") << "')\n\n";

    // -----------------------------------------------------------------------------------
    // 3. THE REAL SHAPE: A TRIGGER PUBLISHES, THE WRITER DOES NOT KNOW
    // -----------------------------------------------------------------------------------
    // `pg_notify(channel, payload)` is the function form of NOTIFY, and it is the one you need
    // inside PL/pgSQL because the statement form does not take an expression for the channel.
    auto made =
        co_await pub.execute(std::string("CREATE TEMP TABLE ") + TABLE + " (id SERIAL PRIMARY KEY, kind TEXT NOT NULL);" + "CREATE FUNCTION "
                             + FUNC + "() RETURNS trigger AS $$ BEGIN" + "  PERFORM pg_notify('" + CHANNEL
                             + "', 'row:' || NEW.id::text || ':' || NEW.kind);" + "  RETURN NEW; END; $$ LANGUAGE plpgsql;" + "CREATE TRIGGER "
                             + TRIGGER + " AFTER INSERT ON " + TABLE + " FOR EACH ROW EXECUTE FUNCTION " + FUNC + "();");
    if (!made.ok()) {
        qb::io::cerr() << "schema setup failed: " << made.error().what() << std::endl;
        co_return;
    }

    // An ordinary INSERT. No application code publishes anything.
    auto inserted = co_await pub.query(std::string("INSERT INTO ") + TABLE + " (kind) VALUES ($1);", std::string("resize-image"));
    std::optional<qb::pg::notification> from_trigger = co_await sub.receive();

    const bool trigger_ok = inserted.ok() && from_trigger.has_value() && from_trigger->payload.rfind("row:", 0) == 0
                            && from_trigger->payload.find("resize-image") != std::string::npos;
    qb::io::cout() << (trigger_ok ? "[trigger] the INSERT itself published: a trigger called pg_notify(), so the writer did\n"
                                    "          not have to know a listener exists. This is the version worth deploying —\n"
                                    "          the announcement cannot drift from the write, because it IS the write\n"
                                  : "[trigger] UNEXPECTED: the INSERT did not produce a notification\n");
    qb::io::cout() << "          (payload '" << (from_trigger ? from_trigger->payload : "?") << "' — an IDENTIFIER, not the row)\n\n";

    // -----------------------------------------------------------------------------------
    // 4. THE TWO RULES YOU FIND OUT ABOUT IN PRODUCTION
    // -----------------------------------------------------------------------------------
    (void) co_await sub.unlisten(CHANNEL);
    const int before_deaf = seen_via_callback;
    (void) co_await pub.notify(CHANNEL, "nobody-is-listening");
    co_await qb::io::async::sleep(150ms);
    const bool lost = seen_via_callback == before_deaf;

    qb::io::cout() << (lost ? "[rule] NOTIFY is NOT a queue — one sent while nobody was listening is gone, and no\n"
                              "       amount of reconnecting will get it back. Anything that must survive a restart is a\n"
                              "       ROW; the notification only says 'go and look'\n"
                            : "[rule] UNEXPECTED: a notification arrived after UNLISTEN\n");

    // ...and the reconnect rule, measured rather than asserted. The connection is dropped from
    // the SERVER side — `pg_terminate_backend` on the consumer's own backend — because that is
    // what a failover or an admin actually does, and the one drop a client cannot prevent.
    // (`sub.disconnect()` would do as well: it completes the teardown inside the call, and is
    // safe from a coroutine like this one.)
    auto      my_pid_row = co_await sub.execute("SELECT pg_backend_pid();");
    const int sub_pid    = my_pid_row.ok() && !my_pid_row.result().empty() ? my_pid_row.result()[0][0].as<int>() : 0;
    (void) co_await pub.query("SELECT pg_terminate_backend($1);", sub_pid);
    co_await qb::io::async::sleep(150ms);

    const bool reconnected     = co_await sub.connect(PG_CONNECTION_STRING);
    const int  before_relisten = seen_via_callback;
    (void) co_await pub.notify(CHANNEL, "after-reconnect-without-relisten");
    co_await qb::io::async::sleep(150ms);
    const bool still_deaf = seen_via_callback == before_relisten;

    // Re-LISTEN, and the notification is delivered again — TO THE CALLBACK.
    (void) co_await sub.listen(CHANNEL);
    const int before_relistened_notify = seen_via_callback;
    (void) co_await pub.notify(CHANNEL, "after-relisten");
    co_await qb::io::async::sleep(150ms);
    const bool callback_recovered = seen_via_callback > before_relistened_notify;

    const bool rule_ok = lost && reconnected && still_deaf && callback_recovered;
    qb::io::cout() << (rule_ok ? "[rule] the subscription belongs to the CONNECTION: after a reconnect you must LISTEN\n"
                                 "       again, and the gap between the drop and the re-LISTEN is a hole you have to\n"
                                 "       reconcile against the table rather than hope about\n"
                               : "[rule] UNEXPECTED: the reconnect/re-LISTEN sequence did not behave as documented\n");
    qb::io::cout() << "       (silent after reconnect: " << (still_deaf ? "yes" : "no")
                   << "; after re-LISTEN the callback fired again: " << (callback_recovered ? "yes" : "no") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. ...AND receive() COMES BACK WITH IT
    // -----------------------------------------------------------------------------------
    // The drop closed the consumer's queue — which is what ends a `receive()` parked on it, with
    // std::nullopt — and a closed queue never reopens (`qb::io::async::channel<T>::close()` is
    // terminal). It does not have to: the consumer outlives its connections, and once connected
    // again it serves the new connection from a fresh queue, carrying over anything received and
    // not yet read. The re-LISTENed notification above went to the callback AND to that queue, so the
    // same object's `co_await receive()` hands it over — no second consumer, no loop to rewire.
    std::optional<qb::pg::notification> after_reconnect;
    if (callback_recovered) // it is queued; without it, receive() would park waiting for nothing
        after_reconnect = co_await sub.receive();

    const bool reuse_ok = after_reconnect.has_value() && after_reconnect->payload == "after-relisten";
    qb::io::cout() << (reuse_ok ? "[reuse] a reconnected consumer's co_await receive() comes back: the drop closed its\n"
                                  "        queue, the reconnect gave it a fresh one, and the notification the re-LISTEN\n"
                                  "        let through was waiting there — the same object, the same loop\n"
                                : "[reuse] UNEXPECTED: the reconnected consumer's receive() did not deliver the re-LISTENed one\n");
    qb::io::cout() << "        (reconnected consumer's receive(): '" << (after_reconnect ? after_reconnect->payload : "nothing") << "')\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    (void) co_await sub.unlisten_all();
    (void) co_await pub.execute(std::string("DROP TRIGGER IF EXISTS ") + TRIGGER + " ON " + TABLE + ";");
    auto function_cleaned = co_await pub.execute(std::string("DROP FUNCTION ") + FUNC + "();");
    auto cleaned          = co_await pub.execute(std::string("DROP TABLE ") + TABLE + ";");

    ok = basic_ok && txn_ok && trigger_ok && rule_ok && reuse_ok && function_cleaned.ok() && cleaned.ok() && sub.is_connected();
    qb::io::cout() << "=== listen/notify complete: the trigger, the function and the table are dropped ===\n"
                   << "(the callback saw " << seen_via_callback << " notification(s) in total)\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_listen_notify(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
