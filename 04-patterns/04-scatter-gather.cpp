/**
 * @file examples/04-patterns/04-scatter-gather.cpp
 * @tier 04-patterns
 * @teaches Request/response between actors as ONE line — `co_await qb::ask(...)` — the four
 *          fan-out shapes built on it (every reply, the first reply, the first k replies, and a
 *          bounded fan-out with at most N outstanding), and the one budget that bounds a whole
 *          CHAIN of asks instead of resetting at every hop.
 * @demonstrates qb::Request<int>, qb::ask, qb::answer, resolve_ask, qb::ask_all, qb::ask_any,
 *               qb::ask_quorum, qb::deadline, qb::deadline_in, qb::remaining, qb::ask_by,
 *               qb::io::async::timeout_error, spawn, qb::ScopedCoroContext,
 *               ctx.sleep, ctx.time, ctx.push_to<Quote>, registerEvent<E>, qb::Main
 * @prerequisites 03-coroutines/02-actor-coroutines, 04-patterns/03-worker-pool
 * @expect "[ask] one request, one reply, one line: BTC ="
 * @expect "[ask_all] 5 venues, unbounded:"
 * @expect "[ask_all] 5 venues, max_in_flight=2:"
 * @expect "[ask_any] first reply wins: venue"
 * @expect "[ask_quorum] first 3 of 5, in completion order: venues"
 * @expect "[ask] a venue that never answers throws timeout_error after"
 * @expect "[deadline] one 150 ms budget, threaded through ask_by: hop 1 left"
 * @expect "[deadline] an already-spent budget fails fast and sends nothing: "
 * @expect "=== scatter/gather complete: no correlation map, no reply handler, no timer ==="
 *
 * WHY THIS FILE MATTERS MORE THAN THE OTHERS
 * ------------------------------------------
 * `qb::ask` had ZERO call sites in the 55 programs that preceded this corpus, and four of them
 * hand-rolled it — one so awkwardly that the worker pushed its reply to itself. What it
 * replaces, every time, is the same three pieces of machinery: a map from a correlation id to
 * "what I was doing", a reply handler that looks the id up and resumes that work, and a timer
 * that cleans the entry up when no reply comes. All three are below, in the library, and the
 * asker's code is a straight line.
 *
 * THE ONE LINE YOU CANNOT LEAVE OUT
 * ---------------------------------
 * The asker must register the exchange type and route replies to itself:
 *
 *     registerEvent<Quote>(*this);
 *     void on(Quote &q) { resolve_ask(q); }
 *
 * `ask` sends the request and parks a coroutine on the correlation id; the reply comes back as
 * an ordinary event to the ASKER, and `resolve_ask` is what hands it to the parked coroutine.
 * Forget it and every `ask` in the actor times out with no other symptom — which is the single
 * most likely way to be confused by this pattern. (`qb::answer` calls `resolve_ask` for you,
 * which is why a responder that is also an asker needs nothing extra.)
 *
 * THE TWO RESPONDER SHAPES, BOTH HERE
 * -----------------------------------
 * `LocalCache` answers synchronously with `qb::answer(*this, q, fn)` — it fills `q.response`
 * and replies, preserving the correlation id. `Venue` cannot answer at once, so it copies the
 * exchange, fills the copy, and pushes it back from a coroutine after its latency. A copy is
 * exactly right here: `correlation_id` is a payload field, so it survives the copy, while the
 * routing header is overwritten by the push.
 *
 * WHAT `max_in_flight` IS FOR
 * ---------------------------
 * `ask_all(ctx, targets, req, timeout, k)` is a true sliding window: all asks are launched, but
 * each waits on a shared semaphore, so a new request starts the instant an earlier one finishes
 * and concurrency never exceeds `k`. The two elapsed numbers this program prints are the whole
 * argument for it — unbounded is faster, bounded is kinder to whatever is downstream, and the
 * gap is the price you are paying for that. NOTE the gate is `qb::detail::gated_ask`, an
 * internal helper: the bounded overload IS the public entry point, there is no `qb::gated_ask`.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-scatter-gather
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-scatter-gather
 */

#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// The exchange. ONE event type round-trips the whole conversation: derive from
// `qb::Request<Resp>`, add the REQUEST fields, and the base supplies the `response` slot plus
// the correlation id. `qb::string<8>`, not std::string — events are memcpy-relocated.
struct Quote : qb::Request<int> {
    qb::string<8> symbol;
    int           venue{-1}; ///< filled in by the responder, so a reply says who sent it
};

// ---------------------------------------------------------------------------
// Responder A: answers synchronously. This is the whole responder.
// ---------------------------------------------------------------------------
class LocalCache : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }

    void
    on(Quote &q) {
        // `answer` routes a reply to one of OUR own asks first (there are none here), then sets
        // q.response = fn(q) and replies the same event, correlation id preserved.
        // `fn` runs inside this handler and must not throw: a throwing actor handler terminates
        // the core. Compute a failure into the payload instead of throwing out of it.
        qb::answer(*this, q, [](Quote const &r) { return std::string_view{r.symbol} == "BTC" ? 64000 : 1; });
    }
};

// ---------------------------------------------------------------------------
// Responder B: cannot answer now. Copies the exchange and pushes it back later.
// ---------------------------------------------------------------------------
class Venue : public qb::Actor {
    int          _index;
    qb::duration _latency;
    int          _price;

public:
    Venue(int index, qb::duration latency, int price)
        : _index(index)
        , _latency(latency)
        , _price(price) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }

    void
    on(Quote &q) {
        Quote reply    = q; // a copy carries the correlation id; the push rewrites the header
        reply.response = _price;
        reply.venue    = _index;
        const auto who = q.getSource();
        const auto lat = _latency;
        // spawn the lambda DIRECTLY (no trailing `()`): the closure is owned by the framework and
        // the frame is bound to this actor's scope, so a kill while parked unwinds it.
        spawn([who, reply, lat](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(lat);
            ctx.push_to<Quote>(who, reply);
        });
    }
};

// A venue that registers the exchange and never answers. Its only job is to make a timeout.
class DeadVenue : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }
    void
    on(Quote &) {}
};

// ---------------------------------------------------------------------------
// The asker. Everything below reads top to bottom, which is the point of the pattern.
// ---------------------------------------------------------------------------
class Trader : public qb::Actor {
    qb::ActorId              _cache;
    qb::ActorId              _dead;
    std::vector<qb::ActorId> _venues;

public:
    Trader(qb::ActorId cache, qb::ActorId dead, std::vector<qb::ActorId> venues)
        : _cache(cache)
        , _dead(dead)
        , _venues(std::move(venues)) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this); // WITHOUT this, every ask below times out and nothing says why
        auto cache  = _cache;
        auto dead   = _dead;
        auto venues = _venues;
        spawn([cache, dead, venues](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- 1. one request, one reply ------------------------------------------------
            auto one = co_await qb::ask(ctx, cache, Quote{.symbol = "BTC"}, 500ms);
            qb::io::cout() << "[ask] one request, one reply, one line: BTC = " << one.response << "\n";

            // ---- 2. every reply -----------------------------------------------------------
            auto t0  = ctx.time();
            auto all = co_await qb::ask_all(ctx, venues, Quote{.symbol = "BTC"}, 2s);
            qb::io::cout() << "[ask_all] 5 venues, unbounded: " << all.size() << " quotes in " << ms_since(ctx, t0)
                           << " ms (as slow as the slowest venue)\n";

            // ---- 3. every reply, at most two requests outstanding --------------------------
            t0          = ctx.time();
            auto capped = co_await qb::ask_all(ctx, venues, Quote{.symbol = "BTC"}, 2s, 2);
            qb::io::cout() << "[ask_all] 5 venues, max_in_flight=2: " << capped.size() << " quotes in " << ms_since(ctx, t0)
                           << " ms (slower on purpose — a sliding window, never a wave)\n";

            // ---- 4. the first reply wins; the losers are torn down -------------------------
            t0        = ctx.time();
            auto fast = co_await qb::ask_any(ctx, venues, Quote{.symbol = "BTC"}, 2s);
            qb::io::cout() << "[ask_any] first reply wins: venue " << fast.venue << " in " << ms_since(ctx, t0)
                           << " ms; the other 4 asks were reclaimed at once\n";

            // ---- 5. the first k replies ---------------------------------------------------
            t0       = ctx.time();
            auto maj = co_await qb::ask_quorum(ctx, venues, 3, Quote{.symbol = "BTC"}, 2s);
            qb::io::cout() << "[ask_quorum] first 3 of 5, in completion order: venues";
            for (auto const &q : maj)
                qb::io::cout() << " " << q.venue;
            qb::io::cout() << " in " << ms_since(ctx, t0) << " ms\n";

            // ---- 6. and when nobody answers -----------------------------------------------
            t0 = ctx.time();
            try {
                (void) co_await qb::ask(ctx, dead, Quote{.symbol = "BTC"}, 120ms);
                qb::io::cout() << "[ask] UNREACHABLE: the dead venue answered\n";
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[ask] a venue that never answers throws timeout_error after " << ms_since(ctx, t0)
                               << " ms — no leaked entry, no dangling timer\n";
            }

            // ---- 7. one budget for the whole chain, not one per hop -----------------------
            // A `timeout` is RELATIVE and starts again at every hop, so three hops at 150 ms each
            // may legitimately take 450 ms. A `qb::deadline` is ABSOLUTE: build it once, thread it
            // through `ask_by`, and each hop gets only what the ones before it left.
            const auto dl = qb::deadline_in(ctx, 150ms);
            (void) co_await qb::ask_by(ctx, venues[0], Quote{.symbol = "BTC"}, dl); // ~60 ms
            qb::io::cout() << "[deadline] one 150 ms budget, threaded through ask_by: hop 1 left " << qb::remaining(dl, ctx).count() / 1'000'000
                           << " ms\n";
            (void) co_await qb::ask_by(ctx, venues[0], Quote{.symbol = "BTC"}, dl); // ~60 ms
            qb::io::cout() << "[deadline] hop 2 left " << qb::remaining(dl, ctx).count() / 1'000'000
                           << " ms — a relative timeout would have reset to 150 here\n";
            try {
                (void) co_await qb::ask_by(ctx, venues[0], Quote{.symbol = "BTC"}, dl); // needs 60
                qb::io::cout() << "[deadline] UNREACHABLE: hop 3 fitted in the budget\n";
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[deadline] hop 3 ran out of budget: the CHAIN is bounded, not each hop\n";
            }
            t0 = ctx.time();
            try {
                // Already spent: this throws without sending anything at all.
                (void) co_await qb::ask_by(ctx, venues[0], Quote{.symbol = "BTC"}, qb::deadline{0});
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[deadline] an already-spent budget fails fast and sends nothing: " << ms_since(ctx, t0) << " ms\n";
            }

            qb::io::cout() << "=== scatter/gather complete: no correlation map, no reply handler, no timer ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    // THE line. Every reply to one of our asks arrives here as an ordinary event; `resolve_ask`
    // hands it to the parked coroutine. It returns false for an event that is not ours, which is
    // where unsolicited handling would go.
    void
    on(Quote &q) {
        (void) resolve_ask(q);
    }

private:
    // `ctx.time()` is the VirtualCore clock, cached once per loop pass — exact enough to compare
    // across an await, and free (no syscall).
    static std::uint64_t
    ms_since(qb::ScopedCoroContext const &ctx, std::uint64_t t0) {
        return (ctx.time() - t0) / 1'000'000ull;
    }
};

int
main() {
    qb::Main engine;

    // Five venues with deliberately different latencies: that spread is what makes `ask_any` and
    // `ask_quorum` mean something, and what the two `ask_all` timings are measured against.
    std::vector<qb::ActorId> venues;
    venues.push_back(engine.addActor<Venue>(0, 0, qb::duration{60ms}, 64010));
    venues.push_back(engine.addActor<Venue>(0, 1, qb::duration{50ms}, 63990));
    venues.push_back(engine.addActor<Venue>(0, 2, qb::duration{40ms}, 64005));
    venues.push_back(engine.addActor<Venue>(0, 3, qb::duration{30ms}, 63995));
    venues.push_back(engine.addActor<Venue>(0, 4, qb::duration{20ms}, 64000));

    auto cache = engine.addActor<LocalCache>(0);
    auto dead  = engine.addActor<DeadVenue>(0);
    engine.addActor<Trader>(0, cache, dead, venues);

    qb::io::cout() << "[main] one cache, five venues (60/50/40/30/20 ms), one that never answers\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
