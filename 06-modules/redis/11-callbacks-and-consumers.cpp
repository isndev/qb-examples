/**
 * @file examples/06-modules/redis/11-callbacks-and-consumers.cpp
 * @tier 06-modules
 * @teaches The half of qbm-redis the other ten programs never touch: the CALLBACK surface. Every
 *          command has a second overload whose FIRST argument is the handler, `await()` is the
 *          drain that replaces `co_await`, `tcp::pipeline` batches without a single coroutine, and
 *          `tcp::cb_consumer` is Pub/Sub for code that has no coroutine to park.
 * @demonstrates qb::redis::tcp::client, qb::redis::tcp::pipeline, qb::redis::tcp::cb_consumer,
 *               connect, await, flush, pending_reply_count,
 *               on_message, on_error, on_disconnected, subscribe, unsubscribe, publish,
 *               set, get, incr, del, lpush, lrange,
 *               qb::redis::Reply<T>, ok, result, error, qb::redis::message,
 *               qb::io::async::init, qb::io::async::run_until
 * @prerequisites 06-modules/redis/01-connect, 06-modules/redis/04-pubsub
 * @expect "[why] not every program is a coroutine. A GUI callback, a legacy worker thread's main"
 * @expect "[callback] the handler is the FIRST argument, not the last: set(cb, key, value). That"
 * @expect "[await] nothing above had run yet — await() is the drain, and it delivered 4 replies"
 * @expect "[order] replies came back in COMMAND order, which is what makes a pipeline readable:"
 * @expect "[pipeline] tcp::pipeline is the same property with a name: 6 commands went out before"
 * @expect "[failure] a command the CLIENT rejects still calls your handler — with a failed Reply,"
 * @expect "[consumer] cb_consumer delivered 3 messages to on_message(), and the sinks must be set"
 * @expect "[consumer] the two consumers are the same object with different doors: co_consumer"
 * @expect "=== callbacks and consumers complete: no coroutine was used, and every key is gone"
 *
 * THERE ARE TWO COMPLETE APIS HERE, NOT ONE API AND A FALLBACK
 * -----------------------------------------------------------
 * Every command in qbm-redis is generated twice (see any file under
 * `qbm/redis/src/qbm/redis/commands/`):
 *
 *     auto  get(const std::string &key);                 // returns an AWAITER  -> co_await
 *     Derived &get(Func &&func, const std::string &key); // takes a HANDLER     -> callback
 *
 * They are the same command, the same connection and the same wire bytes. The difference is who
 * holds the continuation: the coroutine frame, or your lambda.
 *
 * WHY THE HANDLER IS THE FIRST ARGUMENT
 * -------------------------------------
 * Because most of these commands are VARIADIC — `del(k1, k2, k3)`, `mset(pairs...)`,
 * `geoadd(key, members...)`. A trailing-callback convention cannot coexist with a trailing
 * parameter pack, so the callback moved to the front. It reads oddly for about ten minutes and
 * then it reads as the only thing it could have been. The overloads are told apart by
 * `std::enable_if_t<std::is_invocable_v<Func, Reply<T> &&>>`, so passing a non-callable first
 * argument does not silently pick the wrong one — it fails to compile.
 *
 * `await()` IS THE WHOLE STORY OF THIS FILE
 * -----------------------------------------
 * A callback command does not run when you call it. It appends one handler to a queue and writes
 * bytes; the reply arrives on a later turn of the event loop. `await()` turns the loop until every
 * pending reply has been delivered — so a batch of commands followed by one `await()` is a
 * PIPELINE: N requests on the wire, one round trip's worth of waiting, replies in command order.
 * That is not a feature you switch on, it is what the client does; `tcp::pipeline` just gives the
 * pattern a name and a `flush()`.
 *
 * THE RULE THAT KEEPS CALLBACKS HONEST
 * -----------------------------------
 * Your handler is invoked EXACTLY ONCE, on the event-loop thread, whatever happens. A command the
 * client refuses locally (an empty key, an empty argument list) never reaches the server — and it
 * still calls you, with `ok() == false` and a reason. That is deliberate: a guard that silently
 * returned would leave a coroutine parked forever and a callback never fired, which is a hang
 * rather than an error. Section 4 provokes it.
 *
 * TWO CONSUMERS, ONE CONNECTION MODEL
 * -----------------------------------
 * A subscribed Redis connection cannot run ordinary commands, so a consumer is always its own
 * connection. `co_consumer` (06-modules/redis/04-pubsub) hands you messages through
 * `co_await receive()`; `cb_consumer` pushes them into `on_message()`. Set the three sinks BEFORE
 * subscribing and never reassign one from inside its own handler — the dispatch is an
 * unconditional call with no per-event null check, so a sink is never empty and never re-entered.
 *
 * Every key this program writes is under `qb:example:cb:` and is deleted on the way out.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-callbacks-and-consumers
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-callbacks-and-consumers
 */

#include <cstdint>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

namespace {

constexpr const char *REDIS_URI = "tcp://localhost:6379";

constexpr const char *K_GREET = "qb:example:cb:greeting";
constexpr const char *K_COUNT = "qb:example:cb:counter";
constexpr const char *K_LIST  = "qb:example:cb:log";
constexpr const char *CHANNEL = "qb:example:cb:channel";

} // namespace

int
main() {
    qb::io::async::init();

    qb::io::cout() << "[why] not every program is a coroutine. A GUI callback, a legacy worker thread's main\n"
                      "      loop, an audio buffer handler — none of those can co_await. This whole file is\n"
                      "      ordinary straight-line C++: there is not one co_await in it\n\n";

    qb::redis::tcp::client redis{qb::io::uri(REDIS_URI)};

    // ---------------------------------------------------------------------------------------
    // 0. CONNECT, without a coroutine.
    //
    // `connect(handler, ...)` is the callback form; the handler takes a bool. Nothing happens
    // until the loop turns, so `run_until` is what drives it — which is exactly the shape a
    // callback-based program already has around its own main loop.
    // ---------------------------------------------------------------------------------------
    bool connecting = true;
    bool connected  = false;
    redis.connect(
        [&connecting, &connected](bool up) {
            connected  = up;
            connecting = false;
        },
        qb::io::uri(REDIS_URI));
    qb::io::async::run_until(connecting);

    if (!connected) {
        qb::io::cerr() << "Failed to connect to Redis at " << REDIS_URI << "\n";
        return 1;
    }

    // Clean slate, and the first use of the drain: issue, then await.
    redis.del([](qb::redis::Reply<long long> &&) {}, K_GREET, K_COUNT, K_LIST);
    redis.await();

    // ---------------------------------------------------------------------------------------
    // 1. THE CALLBACK-FIRST FORM, and what `await()` actually drains.
    // ---------------------------------------------------------------------------------------
    std::vector<std::string> arrival_order;
    bool                     set_ok = false;
    std::string              greeting;
    long long                counter  = 0;
    long long                list_len = 0;

    // Four commands, four handlers, ZERO round trips so far: each call appends a handler and
    // writes bytes into the socket's output buffer.
    redis.set(
        [&](qb::redis::Reply<qb::redis::status> &&r) {
            set_ok = r.ok();
            arrival_order.emplace_back("set");
        },
        K_GREET, "hello from a callback");

    redis.incr(
        [&](qb::redis::Reply<long long> &&r) {
            counter = r.ok() ? r.result() : -1;
            arrival_order.emplace_back("incr");
        },
        K_COUNT);

    redis.lpush(
        [&](qb::redis::Reply<long long> &&r) {
            list_len = r.ok() ? r.result() : -1;
            arrival_order.emplace_back("lpush");
        },
        K_LIST, "first", "second", "third");

    redis.get(
        [&](qb::redis::Reply<std::optional<std::string>> &&r) {
            greeting = r.value_or(std::string{"<missing>"});
            arrival_order.emplace_back("get");
        },
        K_GREET);

    const std::size_t pending_before = redis.pending_reply_count();
    const bool        nothing_yet    = arrival_order.empty();

    // THE DRAIN. Turns the event loop until every handler above has been invoked.
    redis.await();

    qb::io::cout() << "[callback] the handler is the FIRST argument, not the last: set(cb, key, value). That\n"
                      "           is forced by the variadic commands — del(k1, k2, k3) has no room for a\n"
                      "           trailing anything — and the overloads are told apart by whether argument one\n"
                      "           is invocable with a Reply<T>, so a mistake is a compile error, not a surprise\n";
    qb::io::cout() << "           (set=" << (set_ok ? "OK" : "FAILED") << ", counter=" << counter << ", list length=" << list_len << ", get=\""
                   << greeting << "\")\n\n";

    const bool drained_ok = nothing_yet && arrival_order.size() == 4;
    qb::io::cout() << "[await] nothing above had run yet — await() is the drain, and it delivered 4 replies\n"
                      "        in one pass. Four commands went out BEFORE the first reply came back, so this\n"
                      "        cost one round trip and not four\n";
    qb::io::cout() << "        (handlers invoked before await(): " << (nothing_yet ? "0" : "some — UNEXPECTED")
                   << ", replies pending at the drain: " << pending_before << ")\n\n";

    std::string order_text;
    for (auto const &name : arrival_order)
        order_text += (order_text.empty() ? "" : " -> ") + name;
    const bool order_ok = order_text == "set -> incr -> lpush -> get";

    qb::io::cout() << "[order] replies came back in COMMAND order, which is what makes a pipeline readable:\n"
                      "        Redis answers requests on one connection strictly in the order it received them,\n"
                      "        so handler N sees reply N and you never have to correlate anything yourself\n";
    qb::io::cout() << "        (" << order_text << ")" << (order_ok ? "" : "  <- UNEXPECTED") << "\n\n";

    // ---------------------------------------------------------------------------------------
    // 2. THE SAME PROPERTY WITH A NAME.
    // ---------------------------------------------------------------------------------------
    qb::redis::tcp::pipeline pipe{redis};

    long long   appended = 0;
    std::size_t replies  = 0;
    for (int i = 0; i < 5; ++i) {
        pipe.client().lpush(
            [&](qb::redis::Reply<long long> &&r) {
                ++replies;
                if (r.ok())
                    appended = r.result();
            },
            K_LIST, "batch-" + std::to_string(i));
    }
    std::vector<std::string> tail;
    pipe.client().lrange(
        [&](qb::redis::Reply<std::vector<std::string>> &&r) {
            ++replies;
            if (r.ok())
                tail = r.result();
        },
        K_LIST, 0, 2);

    const std::size_t in_flight = pipe.pending_reply_count();
    pipe.flush(); // same call as redis.await(), reached through the pipeline handle

    qb::io::cout() << "[pipeline] tcp::pipeline is the same property with a name: 6 commands went out before\n"
                      "           any reply came back, and flush() is await() under a different door. It adds no\n"
                      "           protocol — pipelining is what the client already does between drains\n";
    qb::io::cout() << "           (" << in_flight << " replies in flight at flush(), " << replies << " handlers ran, list is now " << appended
                   << " long, newest 3 = ";
    for (std::size_t i = 0; i < tail.size(); ++i)
        qb::io::cout() << (i ? ", " : "") << tail[i];
    qb::io::cout() << ")\n\n";

    // ---------------------------------------------------------------------------------------
    // 3. A COMMAND THE CLIENT ITSELF REFUSES.
    //
    // `del()` with no keys never reaches the server. The handler still runs, once, with a failed
    // Reply — because the alternative (returning quietly) is a callback that never fires and a
    // coroutine that never resumes, which is a hang and not an error.
    // ---------------------------------------------------------------------------------------
    bool        refused_called = false;
    bool        refused_ok     = true;
    std::string refused_reason;
    redis.del([&](qb::redis::Reply<long long> &&r) {
        refused_called = true;
        refused_ok     = r.ok();
        refused_reason = r.error();
    });
    redis.await();

    qb::io::cout() << "[failure] a command the CLIENT rejects still calls your handler — with a failed Reply,\n"
                      "          never by staying silent. A guard that returned quietly would leave a callback\n"
                      "          unfired and a coroutine parked forever: a hang, which is strictly worse than an\n"
                      "          error you can read\n";
    qb::io::cout() << "          (handler ran: " << (refused_called ? "yes" : "no — UNEXPECTED")
                   << ", ok()=" << (refused_ok ? "true — UNEXPECTED" : "false") << ", reason=\"" << refused_reason << "\")\n\n";

    // ---------------------------------------------------------------------------------------
    // 4. PUB/SUB WITH NO COROUTINE — cb_consumer.
    //
    // A subscribed connection cannot run ordinary commands, so this is a second client. The three
    // sinks are set BEFORE subscribe(): they are dispatched unconditionally, with no per-message
    // null check, so they must never be empty and must never be reassigned from inside themselves.
    // ---------------------------------------------------------------------------------------
    qb::redis::tcp::cb_consumer consumer{qb::io::uri(REDIS_URI)};

    std::vector<std::string> received;
    std::size_t              consumer_errors = 0;
    bool                     disconnected    = false;
    bool                     waiting         = true;

    consumer.on_message([&](qb::redis::message &&msg) {
        received.push_back(msg.payload);
        if (received.size() == 3)
            waiting = false;
    });
    consumer.on_error([&consumer_errors](qb::redis::error &&) { ++consumer_errors; });
    consumer.on_disconnected([&disconnected](qb::io::async::event::disconnected &&) { disconnected = true; });

    bool sub_connecting = true;
    bool sub_connected  = false;
    consumer.connect(
        [&](bool up) {
            sub_connected  = up;
            sub_connecting = false;
        },
        qb::io::uri(REDIS_URI));
    qb::io::async::run_until(sub_connecting);

    bool subscribed = false;
    if (sub_connected) {
        consumer.subscribe([&subscribed](qb::redis::Reply<qb::redis::subscription> &&r) { subscribed = r.ok(); }, CHANNEL);
        consumer.await();
    }

    long long delivered_to = 0;
    if (subscribed) {
        for (int i = 1; i <= 3; ++i) {
            redis.publish([&delivered_to](qb::redis::Reply<long long> &&r) { delivered_to = r.ok() ? r.result() : -1; }, CHANNEL,
                          "event-" + std::to_string(i));
        }
        redis.await();
        // The publisher's own drain says the bytes went out; it says nothing about the SUBSCRIBER
        // having read them. This is the wait that does — and it is the same `run_until` that drove
        // the connect, which is the point: one loop, driven by main(), calling back into this file.
        qb::io::async::run_until(waiting);
    }

    const bool consumer_ok = sub_connected && subscribed && received.size() == 3 && consumer_errors == 0;
    qb::io::cout() << "[consumer] cb_consumer delivered 3 messages to on_message(), and the sinks must be set\n"
                      "           BEFORE subscribe(): dispatch is one unconditional indirect call with no\n"
                      "           per-message branch, so a sink is never empty and must never reassign itself\n";
    qb::io::cout() << "           (";
    for (std::size_t i = 0; i < received.size(); ++i)
        qb::io::cout() << (i ? ", " : "") << received[i];
    qb::io::cout() << " — PUBLISH reported " << delivered_to << " subscriber, on_error fired " << consumer_errors << " times)\n";
    if (!consumer_ok)
        qb::io::cerr() << "           UNEXPECTED: the subscription did not deliver what it promised\n";

    qb::io::cout() << "[consumer] the two consumers are the same object with different doors: co_consumer\n"
                      "           buffers into a channel you co_await, cb_consumer calls you. Pick by what your\n"
                      "           caller is, not by taste — and note the one asymmetry, that ~cb_consumer just\n"
                      "           stops calling you while ~co_consumer CLOSES its channel and resumes a waiter\n\n";

    // ---------------------------------------------------------------------------------------
    // Cleanup — on this path and on every early return above, which is why the deletes below are
    // matched by the del() right after connect: a run that dies mid-way leaves nothing for the
    // next one to trip over.
    // ---------------------------------------------------------------------------------------
    if (subscribed) {
        consumer.unsubscribe([](qb::redis::Reply<qb::redis::subscription> &&) {}, CHANNEL);
        consumer.await();
    }

    long long removed = 0;
    redis.del([&removed](qb::redis::Reply<long long> &&r) { removed = r.ok() ? r.result() : -1; }, K_GREET, K_COUNT, K_LIST);
    redis.await();

    const bool ok = set_ok && counter == 1 && list_len == 3 && greeting == "hello from a callback" && drained_ok && order_ok && replies == 6
                    && refused_called && !refused_ok && consumer_ok && removed == 3 && !disconnected;

    qb::io::cout() << "=== callbacks and consumers complete: no coroutine was used, and every key is gone\n"
                      "    again ("
                   << removed << " deleted), so a second run measures the same thing as the first ===\n";

    return ok ? 0 : 1;
}
