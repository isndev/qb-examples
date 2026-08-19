/**
 * @file examples/06-modules/redis/09-reliability.cpp
 * @tier 06-modules
 * @teaches What every other Redis example assumes away: that the server can be unreachable, can
 *          drop your connection, and can make you wait. Bounded connect retry, auto-reconnect,
 *          what happens to a command that was IN FLIGHT when the link died, blocking commands that
 *          park a coroutine without blocking the loop, INFO as a health probe — and TLS.
 * @demonstrates qb::redis::RetryPolicy, with_max_attempts, with_initial_delay, with_multiplier,
 *               with_jitter, with_connect_timeout, with_on_retry,
 *               connect_with_retry, enable_auto_reconnect, disable_auto_reconnect, is_connected,
 *               qb::redis::tcp::client, client_id, client_kill, brpop, rpush, info, del,
 *               qb::redis::Reply<T>, ok, error, result, raw,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::sleep, qb::io::async::task<void>, qb::io::async::task<bool>
 * @prerequisites 06-modules/redis/03-coroutines-and-pipelining
 * @expect "[retry] a bounded RetryPolicy gave up after 3 attempts instead of hanging startup"
 * @expect "[retry] the DEFAULT policy is max_attempts = -1, i.e. FOREVER — fine for a background"
 * @expect "[connect] the same call against a live server succeeds on the first attempt"
 * @expect "[drop] the connection was killed from another client, and the command that was IN"
 * @expect "[drop] raw() == nullptr is the discriminator: this is a dead link, not a nil value"
 * @expect "[drop] auto-reconnect brought the CONNECTION back by itself — but nothing re-sent the"
 * @expect "[block] BRPOP parked the coroutine for 1s and the event loop kept turning:"
 * @expect "[block] and a push from another client wakes it at once, not at the timeout"
 * @expect "[health] INFO is the health probe: it answers on the same connection you already have,"
 * @expect "[tls] TLS is a ONE-LINE change: qb::redis::tcp::ssl::client instead of"
 * @expect "=== reliability complete: bounded retry, a killed link, a parked coroutine, a health"
 *
 * THE ASSUMPTION THIS FILE REMOVES
 * --------------------------------
 * Every other Redis program in this corpus opens with `if (!co_await redis.connect()) return;` and
 * then behaves as though the connection is a fact. In a service it is not: the server restarts,
 * a failover moves it, an idle connection is reaped by a proxy, `CLIENT KILL` happens. The
 * client ships for all of that and none of it had a demonstrator.
 *
 * FOUR THINGS THAT ARE EASY TO GET WRONG, ALL MEASURED BELOW
 * ----------------------------------------------------------
 * 1. `RetryPolicy{}` defaults to `max_attempts = -1` — UNLIMITED. As a startup policy that turns
 *    "the database is misconfigured" into "the process hangs and prints nothing". Bound it.
 * 2. A dropped connection does NOT strand your `co_await`. The client fails every pending reply
 *    (`redis.h:958-974`, `(*entry.handler)(nullptr)`), so the coroutine resumes with `ok() ==
 *    false` and `raw() == nullptr`. That null is the discriminator between "the link died" and
 *    "the value was nil" — the same one `05-transactions` needs to tell a WATCH abort from a
 *    parse error. (The DESTRUCTOR path is the other one: destroying the client discards its
 *    pending handlers uninvoked, which orphans the frame instead of resuming it.)
 * 3. Auto-reconnect restores the CONNECTION, not the REQUEST. Nothing is replayed; if the
 *    command mattered, retrying it is your code's job, and it must be safe to run twice.
 * 4. A blocking command (BRPOP and friends) blocks the SERVER-side call, not your process. The
 *    coroutine parks and the event loop keeps turning — section 4 counts the turns to prove it.
 *
 * A SILENCE WORTH KNOWING ABOUT, AND THE FIVE LINES THAT END IT
 * -------------------------------------------------------------
 * `qb::io::async::coro_scheduler().spawn(t)` — the scaffolding every standalone example here uses
 * — does not observe the task's result. `task<void>::promise_type::unhandled_exception()` stores
 * the exception and nobody ever calls `await_resume()`, so an exception escaping a spawned task
 * is DISCARDED IN SILENCE: the program stops mid-way and exits as if it had finished. Measured
 * while writing `08-sorted-sets-and-ttl`, where a throwing constructor in a `co_await` argument
 * cost twenty minutes because there was nothing to read. `guarded()` below is the fix, and it is
 * worth copying into anything you ship: await the body from a wrapper that catches.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-reliability
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-reliability
 */

#include <chrono>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

constexpr const char *URI      = "tcp://localhost:6379";
constexpr const char *DEAD_URI = "tcp://127.0.0.1:6399"; // nothing listens here, on purpose
// Referenced only from the QB_IO_WITH_SSL branch below, so an SSL-off build has a constant
constexpr const char *TLS_URI [[maybe_unused]] = "rediss://localhost:6380";
constexpr const char *K_QUEUE                  = "qb:example:rel:queue";

/// What the coroutine parked on BRPOP saw when the link was cut under it.
struct ParkedOutcome {
    bool        done{false};
    bool        ok{true};
    bool        raw_null{false};
    std::string error;
};

// THE THREE HELPERS BELOW ARE NAMED FUNCTIONS, NOT LAMBDAS, AND THAT IS NOT A STYLE CHOICE.
// `CoroutineScheduler::spawn` takes a `task<void>&&`, so — unlike `qb::Actor::spawn`, which takes
// the lambda itself — you have to CALL something to get a task. `spawn([&]{...}())` calls a
// TEMPORARY closure: the closure dies at the end of the full expression while the frame it
// created still refers to it, and every `&` capture becomes a dangling reference. Measured here:
// SIGSEGV, three lines after the section that printed. `qb/scripts/check-spawn-dangling-closure.py`
// exists for exactly this shape and rejects it in the examples tree too.
//
// A named coroutine function has no such object: its PARAMETERS are copied into the frame, and a
// reference parameter is safe as long as the referent outlives the frame — which every call below
// arranges by waiting for the helper before returning.

qb::io::async::task<void>
park_on_queue(qb::redis::tcp::client &redis, ParkedOutcome &out) {
    auto reply   = co_await redis.brpop({std::string(K_QUEUE)}, 10);
    out.ok       = reply.ok();
    out.raw_null = (reply.raw() == nullptr);
    out.error    = reply.error();
    out.done     = true;
}

qb::io::async::task<void>
tick_until_stopped(bool &stop, int &ticks, bool &done) {
    while (!stop && ticks < 500) {
        co_await qb::io::async::sleep(std::chrono::milliseconds(20));
        ++ticks;
    }
    done = true;
}

qb::io::async::task<void>
push_job_after(qb::duration delay) {
    co_await qb::io::async::sleep(delay);
    qb::redis::tcp::client producer{qb::io::uri(URI)};
    if (co_await producer.connect())
        (void) co_await producer.rpush(K_QUEUE, "job-1");
}

} // namespace

qb::io::async::task<void>
run_reliability(bool &ok) {
    qb::redis::tcp::client redis{qb::io::uri(URI)};

    // -----------------------------------------------------------------------------------
    // 1. CONNECTING TO SOMETHING THAT IS NOT THERE
    // -----------------------------------------------------------------------------------
    // Every field is set explicitly, because the defaults are not what a startup path wants:
    // unlimited attempts and a 3 s connect timeout mean a misconfigured host is a hang.
    int  attempts = 0;
    auto bounded  = qb::redis::RetryPolicy{}
                        .with_max_attempts(3)
                        .with_initial_delay(40ms)
                        .with_multiplier(2.0)
                        .with_jitter(false) // deterministic output; leave it ON in production
                        .with_connect_timeout(200ms)
                        .with_on_retry([&attempts](int attempt, qb::duration next) {
                           ++attempts;
                           qb::io::cout() << "        attempt " << attempt << " failed, next in "
                                          << std::chrono::duration_cast<std::chrono::milliseconds>(next).count() << "ms\n";
                        });

    qb::redis::tcp::client unreachable{qb::io::uri(DEAD_URI)};
    const auto             t0         = std::chrono::steady_clock::now();
    const bool             gave_up    = !co_await unreachable.connect_with_retry(bounded);
    const auto             gave_up_in = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);

    const bool retry_ok = gave_up && attempts >= 2;
    qb::io::cout() << (retry_ok ? "[retry] a bounded RetryPolicy gave up after 3 attempts instead of hanging startup —\n"
                                  "        the delay doubles each time (40, 80, ...) and is capped by with_max_delay\n"
                                : "[retry] UNEXPECTED: the dead endpoint did not exhaust a 3-attempt budget\n");
    qb::io::cout() << "        (gave up in " << gave_up_in.count() << "ms after " << attempts << " retry callback(s))\n";
    qb::io::cout() << "[retry] the DEFAULT policy is max_attempts = -1, i.e. FOREVER — fine for a background\n"
                      "        reconnect, wrong for a startup path, where it turns a typo in a hostname into a\n"
                      "        process that hangs and says nothing\n\n";

    // -----------------------------------------------------------------------------------
    // 2. THE SAME CALL, AGAINST A LIVE SERVER
    // -----------------------------------------------------------------------------------
    if (!co_await redis.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(3).with_connect_timeout(500ms))) {
        qb::io::cerr() << "Failed to connect to Redis at " << URI << "\n";
        co_return;
    }
    qb::io::cout() << "[connect] the same call against a live server succeeds on the first attempt: there is\n"
                      "          no separate 'retrying' API to remember — connect_with_retry IS the connect\n\n";
    (void) co_await redis.del(K_QUEUE);

    // -----------------------------------------------------------------------------------
    // 3. A KILLED LINK, AND WHAT WAS IN FLIGHT WHEN IT DIED
    // -----------------------------------------------------------------------------------
    redis.enable_auto_reconnect(qb::redis::RetryPolicy{}.with_max_attempts(5).with_initial_delay(50ms).with_jitter(false));

    auto my_id = co_await redis.client_id();

    // Park a BRPOP with a 10 s server-side timeout on an empty key, so it is genuinely in flight
    // when the connection goes. The outcome is recorded in locals of THIS coroutine, which
    // outlives the spawned one because we wait for `parked_done` below.
    ParkedOutcome parked;
    qb::io::async::coro_scheduler().spawn(park_on_queue(redis, parked));

    co_await qb::io::async::sleep(80ms); // let the BRPOP reach the server

    // The kill has to come from somewhere else — CLIENT KILL with SKIPME yes will not cut the
    // connection issuing it, which is exactly what makes this an honest simulation of a failover.
    {
        qb::redis::tcp::client executioner{qb::io::uri(URI)};
        if (co_await executioner.connect())
            (void) co_await executioner.client_kill("", my_id.result());
    }

    for (int i = 0; i < 100 && !parked.done; ++i)
        co_await qb::io::async::sleep(10ms);

    const bool drop_ok = parked.done && !parked.ok;
    qb::io::cout() << (drop_ok ? "[drop] the connection was killed from another client, and the command that was IN\n"
                                 "       FLIGHT came back rather than hanging: the client FAILS every pending reply on\n"
                                 "       disconnect instead of dropping it\n"
                               : "[drop] UNEXPECTED: the parked BRPOP did not come back after the kill\n");
    qb::io::cout() << (parked.raw_null ? "[drop] raw() == nullptr is the discriminator: this is a dead link, not a nil value.\n"
                                         "       ok() alone cannot tell those apart, and one of them is retryable\n"
                                       : "[drop] UNEXPECTED: the failed reply carried a raw value\n");
    qb::io::cout() << "       (the error text was: " << (parked.error.empty() ? "<empty>" : parked.error) << ")\n";

    // Auto-reconnect runs as a detached task on this same loop, so it needs turns to finish.
    for (int i = 0; i < 100 && !redis.is_connected(); ++i)
        co_await qb::io::async::sleep(10ms);
    auto after = co_await redis.info("server");

    const bool back = redis.is_connected() && after.ok();
    qb::io::cout() << (back ? "[drop] auto-reconnect brought the CONNECTION back by itself — but nothing re-sent the\n"
                              "       BRPOP. A reconnect restores the link, never the request, so a retry is your\n"
                              "       code's job and the command must be safe to run twice\n"
                            : "[drop] UNEXPECTED: the client did not reconnect by itself\n");
    redis.disable_auto_reconnect();
    qb::io::cout() << "\n";

    // -----------------------------------------------------------------------------------
    // 4. A BLOCKING COMMAND DOES NOT BLOCK THE LOOP
    // -----------------------------------------------------------------------------------
    // BRPOP with a 1 s timeout on an empty key. While it is parked, a second coroutine wakes
    // every 20 ms on the SAME thread and counts. If a blocking command blocked the process, that
    // counter would still be 0 when the BRPOP returns.
    int  ticks       = 0;
    bool ticker_stop = false, ticker_done = false;
    qb::io::async::coro_scheduler().spawn(tick_until_stopped(ticker_stop, ticks, ticker_done));

    const auto b0      = std::chrono::steady_clock::now();
    auto       nothing = co_await redis.brpop({std::string(K_QUEUE)}, 1);
    const auto waited  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - b0);
    const int  during  = ticks;

    // A BRPOP that times out is an EMPTY result, not a failure: ok() is true and the optional is
    // empty. Treating the timeout as an error is how a poller starts logging at 1 Hz forever.
    const bool block_ok = nothing.ok() && !nothing.result().has_value() && during >= 10 && waited >= 900ms;
    qb::io::cout() << (block_ok ? "[block] BRPOP parked the coroutine for 1s and the event loop kept turning:\n"
                                  "        a second coroutine on the same thread ticked throughout. A timeout is an\n"
                                  "        EMPTY result with ok() == true, not an error\n"
                                : "[block] UNEXPECTED: the 1s BRPOP did not park while the loop kept running\n");
    qb::io::cout() << "        (waited " << waited.count() << "ms, " << during << " ticks of another coroutine meanwhile)\n";

    // ...and the wake-up path: a push from elsewhere ends the wait immediately.
    qb::io::async::coro_scheduler().spawn(push_job_after(120ms));

    const auto w0     = std::chrono::steady_clock::now();
    auto       popped = co_await redis.brpop({std::string(K_QUEUE)}, 5);
    const auto woke   = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - w0);

    const bool woke_ok = popped.ok() && popped.result().has_value() && popped.result()->second == "job-1" && woke < 2000ms;
    qb::io::cout() << (woke_ok ? "[block] and a push from another client wakes it at once, not at the timeout — which\n"
                                 "        is what makes BRPOP a work queue rather than a poll\n"
                               : "[block] UNEXPECTED: the pushed job did not wake the waiting BRPOP\n");
    qb::io::cout() << "        (woke after " << woke.count() << "ms with '" << (popped.result() ? popped.result()->second : "?") << "')\n";
    ticker_stop = true;
    for (int i = 0; i < 100 && !ticker_done; ++i)
        co_await qb::io::async::sleep(10ms);
    qb::io::cout() << "\n";

    // -----------------------------------------------------------------------------------
    // 5. INFO — the health probe you already have
    // -----------------------------------------------------------------------------------
    // The reply is the raw INFO text as a json string, not a parsed object: `section:key:value`
    // lines. Ask for ONE section — the full INFO is large and you do not want it on a timer.
    qb::redis::Reply<qb::json> server_info = co_await redis.info("server");
    const std::string          text = server_info.result().is_string() ? server_info.result().get<std::string>() : server_info.result().dump();
    const auto                 vpos = text.find("redis_version:");
    const std::string          version = vpos == std::string::npos ? "?" : text.substr(vpos + 14, text.find('\r', vpos) - vpos - 14);

    const bool health_ok = server_info.ok() && vpos != std::string::npos;
    qb::io::cout() << (health_ok ? "[health] INFO is the health probe: it answers on the same connection you already have,\n"
                                   "         so it proves the LINK as well as the server. Ask for one section, on an\n"
                                   "         interval you would be happy to see in the slow log\n"
                                 : "[health] UNEXPECTED: INFO server did not report a redis_version\n");
    qb::io::cout() << "         (redis_version " << version << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 6. TLS
    // -----------------------------------------------------------------------------------
    qb::io::cout() << "[tls] TLS is a ONE-LINE change: qb::redis::tcp::ssl::client instead of\n"
                      "      qb::redis::tcp::client, and a rediss:// URI. Every command above is identical —\n"
                      "      set_ssl_root_cert() adds a private CA and set_ssl_client_certificate() does mTLS\n";
#ifdef QB_HAS_SSL
    {
        // Attempted, not assumed: most developer machines run a plaintext Redis, so this reports
        // what it MEASURED rather than pretending. A TLS Redis listens on 6380 by convention.
        qb::redis::tcp::ssl::client secure{qb::io::uri(TLS_URI)};
        const bool                  up =
            co_await secure.connect_with_retry(qb::redis::RetryPolicy{}.with_max_attempts(1).with_connect_timeout(300ms).with_jitter(false));
        qb::io::cout() << "      (probe of " << TLS_URI << ": "
                       << (up ? "a TLS Redis answered" : "no TLS Redis on this host — expected on a default install") << ")\n";
    }
#else
    qb::io::cout() << "      (this build has QB_HAS_SSL off, so tcp::ssl::client does not exist in it)\n";
#endif

    (void) co_await redis.del(K_QUEUE);
    ok = retry_ok && drop_ok && parked.raw_null && back && block_ok && woke_ok && health_ok;
    qb::io::cout() << "\n=== reliability complete: bounded retry, a killed link, a parked coroutine, a health\n"
                      "    probe and the TLS seam ===\n";
    co_return;
}

// The five lines the header argues for. `spawn` never looks at a task's result, so this wrapper —
// which AWAITS the body — is the only thing between a thrown exception and total silence.
qb::io::async::task<void>
guarded(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};
    try {
        co_await run_reliability(ok);
    } catch (std::exception const &e) {
        qb::io::cerr() << "FATAL: an exception escaped the example body: " << e.what() << "\n";
        ok = false;
    }
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(guarded(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
