/**
 * @file examples/06-modules/pgsql/07-listen-notify.cpp
 * @tier 06-modules
 * @teaches PostgreSQL as an event bus: LISTEN/NOTIFY, a `notify_co_consumer` you `co_await`, a
 *          TRIGGER that publishes on every INSERT — and the four rules that decide whether that
 *          is a good idea (transactional delivery, no durability, an 8000-byte payload, and a
 *          subscription that belongs to the session: the client replays it after a reconnection
 *          and marks the hole the server cannot replay).
 * @demonstrates qb::pg::tcp::database, qb::pg::tcp::notify_co_consumer, qb::pg::notification,
 *               qb::pg::notification_kind, qb::pg::RetryPolicy, qb::io::gap_reason,
 *               qb::io::down_reason, listen, unlisten, unlisten_all, notify, on_notify,
 *               on_notify_dropped, receive, notify_channel_capacity, enable_auto_reconnect,
 *               on_connected, listened_channels, backend_pid, disconnect, down_reason, begin,
 *               commit, rollback, execute, is_connected, qb::io::async::init,
 *               qb::io::async::run_until, qb::io::async::coro_scheduler, qb::io::async::sleep,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/03-transactions
 * @expect "Connected: one publisher, one notify_co_consumer, one channel."
 * @expect "[basic] LISTEN then NOTIFY: the consumer woke on co_await receive() with the channel,"
 * @expect "[txn] a NOTIFY inside a transaction is delivered at COMMIT and never at all if the"
 * @expect "[trigger] the INSERT itself published: a trigger called pg_notify(), so the writer did"
 * @expect "[rule] NOTIFY is NOT a queue — one sent while nobody was listening is gone, and no"
 * @expect "[rule] the subscription belongs to the SESSION, and the client keeps it: after the"
 * @expect "[session] disconnect() ends the session: down(user), the stream ends, the channels are"
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
 * 4. THE SUBSCRIPTION BELONGS TO THE SESSION. A new backend listens on nothing — so the client
 *    keeps what `listen()` left and replays it on every new session, before any other query. What
 *    it cannot replay is what the server published while no session listened: the stream says so
 *    with a GAP MARKER, which is exactly the moment to reconcile against the table.
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
 * `receive()` parks on the consumer's queue, and both a connection that goes DOWN and
 * `~notify_co_consumer` CLOSE that queue — so unlike every other module awaiter here, a parked
 * `receive()` does resume, with `std::nullopt`. That makes `while (auto n = co_await sub.receive())`
 * a loop that ends by itself, rather than an orphaned frame. A loss an armed reconnection run
 * recovers is NOT down: the loop goes on, and reads a gap marker where the hole is (section 4).
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
    // behind — and the stream then carries a gap marker where the dropped ones were.
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

    // ...and the fourth rule: the subscription belongs to the SESSION. The client keeps what
    // listen() / unlisten() left and replays it on every new session; an armed reconnection run is
    // what makes a new session after a loss. The connection is dropped from the SERVER side —
    // `pg_terminate_backend` on the consumer's own backend — because that is what a failover or an
    // admin actually does, and the one drop a client cannot prevent.
    const bool relistened  = (co_await sub.listen(CHANNEL)).ok();
    int        reconnected = 0;
    sub.on_connected([&reconnected] { ++reconnected; }); // installed now: it counts the reconnections only
    sub.enable_auto_reconnect(qb::pg::RetryPolicy{}.with_initial_delay(20ms).with_jitter(false));
    (void) co_await pub.query("SELECT pg_terminate_backend($1);", sub.backend_pid());
    for (int i = 0; i < 300 && reconnected == 0; ++i) // the run reconnects in milliseconds; bounded
        co_await qb::io::async::sleep(10ms);
    (void) co_await pub.notify(CHANNEL, "after-reconnect");

    // The same loop goes on: the stream did not end. Its next element is the HOLE — whatever the
    // server published while no session listened is gone — and the reader resynchronises from the
    // source of truth right there, on the same connection (it is ready when the marker arrives).
    // Here the "truth" checked is the server's own: the new backend listens on the channel.
    std::optional<qb::pg::notification> hole = co_await sub.receive();
    auto        listening                    = co_await sub.query("SELECT coalesce(string_agg(c, ','), '') FROM pg_listening_channels() AS c;");
    std::string replayed = listening.ok() && listening.result().size() == 1 ? listening.result()[0][0].as<std::string>() : "?";
    std::optional<qb::pg::notification> after = co_await sub.receive();

    const bool rule_ok = lost && relistened && reconnected == 1 && hole.has_value() && hole->kind == qb::pg::notification_kind::gap
                         && hole->gap.reason == qb::io::gap_reason::reconnect && replayed == CHANNEL && after.has_value()
                         && after->payload == "after-reconnect" && sub.listened_channels().count(CHANNEL) == 1;
    qb::io::cout() << (rule_ok ? "[rule] the subscription belongs to the SESSION, and the client keeps it: after the\n"
                                 "       server dropped the consumer, the armed run reconnected and replayed the LISTEN,\n"
                                 "       and the stream marked the hole it cannot fill — reconcile against the table there\n"
                               : "[rule] UNEXPECTED: the reconnection did not replay the LISTEN behind a gap marker\n");
    qb::io::cout() << "       (stream after the drop: " << (hole && hole->kind == qb::pg::notification_kind::gap ? "a gap marker" : "?")
                   << ", then '" << (after ? after->payload : "nothing") << "'; the new backend listens on '" << replayed << "')\n\n";

    // -----------------------------------------------------------------------------------
    // 5. AN EXPLICIT disconnect() ENDS THE SESSION
    // -----------------------------------------------------------------------------------
    // Whatever the policy, disconnect() is terminal — `down`, reason `user`, no run — and the
    // session's channels go with it. The stream ends: what it held is read, then std::nullopt. A
    // later connect() is a FRESH session: nothing is replayed, and LISTEN is yours to issue again.
    sub.disconnect();
    std::optional<qb::pg::notification> ended = co_await sub.receive();
    const bool forgotten = !ended.has_value() && sub.down_reason() == qb::io::down_reason::user && sub.listened_channels().empty();
    const bool back      = co_await sub.connect(PG_CONNECTION_STRING);

    const bool session_ok = forgotten && back;
    qb::io::cout() << (session_ok ? "[session] disconnect() ends the session: down(user), the stream ends, the channels are\n"
                                    "          forgotten — and the next connect() is a fresh session that replays nothing\n"
                                  : "[session] UNEXPECTED: disconnect() did not end the session\n");
    qb::io::cout() << "          (listened channels after disconnect(): " << sub.listened_channels().size() << ")\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    (void) co_await sub.unlisten_all();
    (void) co_await pub.execute(std::string("DROP TRIGGER IF EXISTS ") + TRIGGER + " ON " + TABLE + ";");
    auto function_cleaned = co_await pub.execute(std::string("DROP FUNCTION ") + FUNC + "();");
    auto cleaned          = co_await pub.execute(std::string("DROP TABLE ") + TABLE + ";");

    ok = basic_ok && txn_ok && trigger_ok && rule_ok && session_ok && function_cleaned.ok() && cleaned.ok() && sub.is_connected();
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
