/**
 * @file examples/04-patterns/08-batching-and-idempotency.cpp
 * @tier 04-patterns
 * @teaches Two patterns that belong together because retrying is what makes both necessary:
 *          `qb::batcher<T>` coalesces many small items into one costly write (on a count OR a
 *          time trigger, whichever comes first), and `qb::dedup_map` + `qb::answer_idempotent`
 *          make a responder run its side effect at most once per key, however many times the
 *          same request arrives.
 * @demonstrates qb::batcher<int>, add, flush, pending, qb::dedup_map<std::uint64_t, int>,
 * qb::answer_idempotent, qb::ask, qb::ask_retry, qb::retry_policy,
 * resolve_ask, qb::Request<int>, context(), spawn, registerEvent<E>, push<E>, qb::Main
 * @prerequisites 04-patterns/05-resilience
 * @expect " rows written as ONE batch (count trigger: the buffer filled)"
 * @expect " rows written as ONE batch (window trigger: nothing filled it in time)"
 * @expect " rows written as ONE batch (manual flush: the shutdown drain)"
 * @expect " -> swallowed; the caller will time out and ask_retry will send it again"
 * @expect " -> effect RAN, and its response was cached under the key"
 * @expect " -> replayed from the cache; the effect did NOT run"
 * @expect " effects executed, dedup cache holds "
 * @expect "=== batching + idempotency complete: fewer writes, exactly-once effects ==="
 *
 * WHY THESE TWO SHARE A FILE
 * --------------------------
 * `04-patterns/05-resilience` retries a request that timed out. A timeout does not mean the
 * request was not received — it means no REPLY arrived, and the most common reason is that the
 * work was done and the answer was lost. So every retry policy you add is a duplicate-effect
 * bug you have just introduced, unless the responder can recognise the repeat. That is
 * `answer_idempotent`, and the key it recognises must be one YOU set: `ask_retry` copies the
 * request per attempt (so your `idempotency_key` survives) but each attempt gets a FRESH
 * correlation id, which is deliberately not something a responder can dedup on.
 *
 * BATCHING: THE TWO TRIGGERS, AND WHY BOTH
 * ----------------------------------------
 * A count trigger alone stalls the tail of a stream forever when traffic stops; a time trigger
 * alone writes tiny batches under load. `batcher{max, window, on_flush}` fires on whichever
 * comes first, so it is bounded in both size and latency. Three details worth knowing:
 *   * THE COUNT FLUSH IS SYNCHRONOUS — `on_flush` runs inside `add()`. The window flush runs
 *     from a coroutine on the actor's own scope, which is why a killed actor DROPS its buffered
 *     items rather than flushing them from a dead actor. Call `flush()` in your shutdown path
 *     if the tail matters; the last section does exactly that.
 *   * `on_flush` MAY capture `this`, unlike a `CircuitBreaker` or `rate_limiter` captured into
 *     a coroutine. The window timer is scope-bound to the actor, so it cannot fire after the
 *     actor is gone. Hold a batcher as an actor MEMBER and that is guaranteed.
 *   * A manual `flush()` bumps the batch generation, so the window timer still pending for that
 *     batch finds itself stale and does nothing. There is no double flush to guard against.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-batching-and-idempotency
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-batching-and-idempotency
 */

#include <chrono>
#include <cstdint>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

using namespace std::chrono_literals;

// ---- part 1: batching -----------------------------------------------------
// NOTE THE FIELD NAME, AND DO NOT "SIMPLIFY" IT TO `id`. `qb::Event` carries five private
// routing fields — `state`, `bucket_size`, `id`, `dest`, `source` — and a derived member of the
// same name HIDES the base one. `VirtualCore::fill_event` then writes the event's type id into
// YOUR field and leaves the routing header unset, so every instance of the event is constructed,
// pushed, and never delivered. Measured here while writing this file: with the member called
// `id`, all thirteen rows below vanished with no warning at compile time (not even under
// -Wshadow-field, because the base fields are private), no error at run time, and no diagnostic
// anywhere. Renaming it to `row_id` fixed it completely.
//
// THAT SILENCE IS NOW CLOSED, and this note is kept as the worked example rather than as a live
// hazard: `qb::detail::routing_safe_type_id<T>` (Event.h) turns each of those five names — plus
// `ServiceEvent`'s `forward` and `service_event_id` — into a `static_assert` naming the field and
// the type, at all three sites that stamp the header (`fill_event`, `Pipe::push`,
// `Pipe::allocated_push`). Writing `int id;` here no longer drops rows; it fails to compile.
struct Row : public qb::Event {
    int row_id;
    explicit Row(int i)
        : row_id(i) {}
};
struct Drain : public qb::Event {};   // "we are shutting down: write the tail"
struct FlushDone : public qb::Event { // writer -> feeder, one per flush
    int n;
    int rows;
    FlushDone(int a, int b)
        : n(a)
        , rows(b) {}
};

// ---- part 2: idempotency --------------------------------------------------
struct Charge : qb::Request<int> {
    std::uint64_t idempotency_key{0}; ///< STABLE across retries; 0 means "do not dedup"
    int           cents{0};
};
struct Census : public qb::Event {};
struct Go : public qb::Event {};

// ---------------------------------------------------------------------------
// The batching writer. One member, one call in the handler, and the expensive thing happens
// once per batch instead of once per row.
// ---------------------------------------------------------------------------
class Writer : public qb::Actor {
    qb::ActorId _feeder;
    int         _flushes   = 0;
    bool        _in_add    = false; ///< set around add(): a flush from here is the COUNT trigger
    bool        _in_manual = false; ///< set around flush(): a flush from here is the manual drain

    // `[this]` is safe here and nowhere near a coroutine: the window timer is bound to this
    // actor's scope, so the flush cannot outlive the actor holding the batcher.
    qb::batcher<int> _batch{4, qb::duration{60ms}, [this](std::vector<int> &&rows) { write_batch(std::move(rows)); }};

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Row>(*this);
        registerEvent<Drain>(*this);
        co_return true;
    }

    void
    on(Row const &r) {
        _feeder = r.getSource(); // learned, not plumbed
        _in_add = true;
        _batch.add(context(), r.row_id); // may flush RIGHT HERE if this row fills the buffer
        _in_add = false;
    }

    void
    on(Drain const &) {
        _in_manual = true;
        _batch.flush(); // no-op on an empty buffer; never a double flush
        _in_manual = false;
    }

private:
    void
    write_batch(std::vector<int> &&rows) {
        ++_flushes;
        const char *why = _in_add      ? " rows written as ONE batch (count trigger: the buffer filled)"
                          : _in_manual ? " rows written as ONE batch (manual flush: the shutdown drain)"
                                       : " rows written as ONE batch (window trigger: nothing filled it in time)";
        qb::io::cout() << "[batch] flush " << _flushes << ": " << rows.size() << why << ", " << _batch.pending() << " still buffered\n";
        push<FlushDone>(_feeder, _flushes, static_cast<int>(rows.size()));
    }
};

// Drives the writer, then hands over to part 2. Every step is driven by a flush notification,
// so nothing here waits on a clock.
class Feeder : public qb::Actor {
    qb::ActorId _writer;
    qb::ActorId _client;
    int         _rows_sent = 0;

public:
    Feeder(qb::ActorId writer, qb::ActorId client)
        : _writer(writer)
        , _client(client) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<FlushDone>(*this);
        send_rows(10); // -> flush at 4, flush at 8, and 2 left over for the window to pick up
        co_return true;
    }

    void
    on(FlushDone const &f) {
        if (f.n == 3) {           // the window flush of the 2 left-over rows
            send_rows(3);         // a new partial batch...
            push<Drain>(_writer); // ...and a shutdown that must not lose it
        } else if (f.n == 4) {
            push<Go>(_client);
        }
    }

private:
    void
    send_rows(int n) {
        for (int i = 0; i < n; ++i)
            push<Row>(_writer, ++_rows_sent);
    }
};

// ---------------------------------------------------------------------------
// The idempotent responder. `answer_idempotent` is `answer` plus one lookup.
// ---------------------------------------------------------------------------
class Bank : public qb::Actor {
    qb::dedup_map<std::uint64_t, int> _seen{1024}; // bounded LRU: keys expire by pressure, not time
    int                               _arrivals = 0;
    int                               _effects  = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Charge>(*this);
        registerEvent<Census>(*this);
        co_return true;
    }

    void
    on(Charge &e) {
        ++_arrivals;
        qb::io::cout() << "[bank] arrival " << _arrivals << " key=" << e.idempotency_key;
        if (_arrivals == 1) {
            // The first attempt is dropped on the floor. This is what a lost request or a lost
            // reply looks like from here, and it is why the caller retries.
            qb::io::cout() << " -> swallowed; the caller will time out and ask_retry will send it again\n";
            return;
        }
        // Narration only, and free of side effects: `contains` asks the same question
        // `answer_idempotent` is about to ask, and (unlike `find`) it does not promote the LRU.
        const bool cached = _seen.contains(e.idempotency_key) && e.idempotency_key != 0;
        qb::io::cout() << (cached ? " -> replayed from the cache; the effect did NOT run\n"
                                  : " -> effect RAN, and its response was cached under the key\n");
        qb::answer_idempotent(*this, e, _seen, [this](Charge const &r) {
            ++_effects; // THE side effect. It must happen at most once per key.
            return r.cents;
        });
    }

    void
    on(Census const &) {
        qb::io::cout() << "[bank] " << _arrivals << " arrivals, " << _effects << " effects executed, dedup cache holds " << _seen.size()
                       << " of " << _seen.capacity()
                       << " keys — the keyed charge cost ONE effect across "
                          "three arrivals; the keyless one always costs its own\n";
        qb::io::cout() << "=== batching + idempotency complete: fewer writes, exactly-once effects ===\n";
        qb::Main::stop();
    }
};

// The caller: one retried charge, one duplicate of it, and one with no key at all.
class Client : public qb::Actor {
    qb::ActorId _bank;

public:
    explicit Client(qb::ActorId bank)
        : _bank(bank) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Charge>(*this);
        registerEvent<Go>(*this);
        co_return true;
    }

    void
    on(Go const &) {
        auto bank = _bank;
        spawn([bank](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Charge order;
            order.idempotency_key = 90210; // stable: the SAME value on every attempt
            order.cents           = 4999;

            // Attempt 1 is swallowed; ask_retry sends the same request again, key included.
            auto first = co_await qb::ask_retry(ctx, bank, order, qb::duration{60ms},
                                                qb::retry_policy{.max_attempts = 3, .backoff = qb::duration{20ms}});
            // A duplicate from the client's own retry logic, minutes later, same key: the
            // responder replays its cached answer instead of charging the card twice.
            auto again = co_await qb::ask(ctx, bank, order, qb::duration{1s});

            // No key at all: `answer_idempotent` behaves exactly like `answer`.
            Charge anonymous;
            anonymous.cents = 4999;
            auto third      = co_await qb::ask(ctx, bank, anonymous, qb::duration{1s});

            qb::io::cout() << "[client] keyed charge answered " << first.response << " then " << again.response
                           << " (same value, one effect); keyless charge answered " << third.response << " and ran an effect of its own\n";
            ctx.push_to<Census>(bank);
        });
    }

    void
    on(Charge &e) {
        (void) resolve_ask(e);
    }
};

int
main() {
    qb::Main engine;

    auto bank   = engine.addActor<Bank>(0);
    auto client = engine.addActor<Client>(0, bank);
    auto writer = engine.addActor<Writer>(0);
    engine.addActor<Feeder>(0, writer, client);

    qb::io::cout() << "[main] 13 rows through a batcher of 4, then 3 charges with 1 effect\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
