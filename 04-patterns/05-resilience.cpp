/**
 * @file examples/04-patterns/05-resilience.cpp
 * @tier 04-patterns
 * @teaches The four things you do to a call that might fail: retry it with backoff, stop
 *          calling a dependency that is down, slow yourself to a rate it can take, and cap how
 *          many calls are in flight at once — each a policy object rather than a piece of
 *          hand-written bookkeeping.
 * @demonstrates qb::retry_policy, qb::ask_retry, qb::CircuitBreaker, qb::ask_guarded,
 *               qb::circuit_open_error, qb::rate_limiter, qb::bulkhead, qb::answer, resolve_ask,
 *               qb::io::async::timeout_error, qb::io::async::when_all, spawn, ctx.sleep, ctx.time
 * @prerequisites 04-patterns/04-scatter-gather
 * @expect "[retry] attempt 3 of at most 4 succeeded after"
 * @expect "[breaker] 2 failures tripped it; the 3rd call threw circuit_open_error in "
 * @expect "[breaker] the open circuit sent NOTHING: the service saw"
 * @expect "[breaker] after the cooldown a half-open trial succeeded; state="
 * @expect "[rate_limiter] 6 acquires, burst 2 then 1 per 25 ms:"
 * @expect "[bulkhead] 5 operations, cap 2: peak concurrency"
 * @expect "=== resilience complete: four policies, none of them hand-rolled ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * The rate limiter inside `examples/06-modules/http/04-middleware.cpp:181-182`, which keeps its
 * whole state in two function-local `static`s — ONE bucket shared by every client of the
 * process, owned by nobody, reset by nothing, and invisible to the actor that believes it owns
 * it. `qb::rate_limiter` is a member you can hold per client, per route or per dependency, and
 * it is a token bucket rather than a fixed window, so a burst is smoothed instead of being
 * cliff-edged at a second boundary.
 *
 * THE FIFTH WAY, AND WHERE IT IS
 * ------------------------------
 * The other way to bound a call is a `qb::deadline`: ONE absolute budget threaded through a
 * whole chain with `ask_by`, instead of a relative timeout that resets at every hop. It belongs
 * to `request.h` rather than to `resilience.h`, so it is the last section of
 * `04-patterns/04-scatter-gather`, whose last section prints the budget shrinking hop by hop
 * until it runs out. Read that one first; this file starts where it stops.
 *
 * THREE SMALLER THINGS THAT ARE EASY TO GET WRONG
 * -----------------------------------------------
 * * `ask_retry` retries ONLY on `timeout_error`. A kill propagates at once, and a responder
 *   that replies "no" quickly is not a failure — encode a business failure in the response
 *   payload, and it will (correctly) not be retried.
 * * Every retry attempt re-sends the request with a FRESH correlation id, so a responder with
 *   a side effect runs it once per attempt. That is what `04-patterns/08` is for.
 * * A `CircuitBreaker` and a `rate_limiter` are plain state machines with no timer of their
 *   own: they are driven by the `VirtualCore` clock you hand them (`ctx.time()`). Hold both by
 *   `shared_ptr` and capture them BY VALUE into a coroutine, because the coroutine may outlive
 *   the actor that started it.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-resilience
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-resilience
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

using namespace std::chrono_literals;

// One exchange type for the whole file: request field `arg`, response slot from Request<int>.
struct Call : qb::Request<int> {
    int arg{0};
};

// Told to a Moody service to make it start answering again.
struct Recover : public qb::Event {};

// ---------------------------------------------------------------------------
// A service that fails the first N requests by not answering at all. Not answering (rather
// than answering "error") is what a timeout looks like, and a timeout is what retry retries.
// ---------------------------------------------------------------------------
class FlakyService : public qb::Actor {
    int _fail_first;
    int _seen = 0;

public:
    explicit FlakyService(int fail_first)
        : _fail_first(fail_first) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Call>(*this);
        co_return true;
    }

    void
    on(Call &e) {
        if (++_seen <= _fail_first) {
            qb::io::cout() << "[flaky] request " << _seen << " swallowed (this is the timeout)\n";
            return; // no reply -> the asker's per-attempt timeout fires -> ask_retry retries
        }
        qb::io::cout() << "[flaky] request " << _seen << " answered\n";
        qb::answer(*this, e, [](Call const &r) { return r.arg * 2; });
    }
};

// ---------------------------------------------------------------------------
// A service that is down until told otherwise. It counts and PRINTS every arrival, which is
// how the next section proves an open circuit sends nothing rather than merely failing fast.
// ---------------------------------------------------------------------------
class MoodyService : public qb::Actor {
    bool _answering = false;
    int  _seen      = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Call>(*this);
        registerEvent<Recover>(*this);
        co_return true;
    }

    // Pushed by the caller immediately after the call the OPEN breaker rejected, so this count
    // is the evidence: three calls were made and only two requests ever left the caller.
    void
    on(Recover const &) {
        _answering = true;
        qb::io::cout() << "[breaker] the open circuit sent NOTHING: the service saw " << _seen << " requests for 3 calls\n";
    }

    void
    on(Call &e) {
        ++_seen;
        if (!_answering)
            return;
        qb::answer(*this, e, [](Call const &r) { return r.arg + 1; });
    }
};

// One bulkheaded operation, as a free coroutine so `when_all` can hold a vector of them. The
// counters are plain ints behind a shared_ptr and NOT atomics: every coroutine here runs on one
// VirtualCore's single thread, which is the whole actor bargain.
struct Peak {
    int in_flight = 0;
    int max       = 0;
};

static qb::io::async::task<void>
guarded_op(qb::ScopedCoroContext ctx, std::shared_ptr<qb::bulkhead> bh, std::shared_ptr<Peak> peak) {
    auto slot = co_await bh->enter(ctx); // parks while the bulkhead is full; freed on scope exit
    if (++peak->in_flight > peak->max)
        peak->max = peak->in_flight;
    co_await ctx.sleep(30ms);
    --peak->in_flight;
}

static const char *
state_name(qb::CircuitBreaker::State s) {
    switch (s) {
        case qb::CircuitBreaker::State::closed:
            return "closed";
        case qb::CircuitBreaker::State::open:
            return "open";
        default:
            return "half_open";
    }
}

// ---------------------------------------------------------------------------
// The caller. Five sections, each a handful of lines, none of them hand-rolled.
// ---------------------------------------------------------------------------
class Caller : public qb::Actor {
    qb::ActorId _flaky;
    qb::ActorId _moody;

public:
    Caller(qb::ActorId flaky, qb::ActorId moody)
        : _flaky(flaky)
        , _moody(moody) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Call>(*this); // without this every ask below times out — see 04-scatter-gather
        auto flaky = _flaky;
        auto moody = _moody;
        spawn([flaky, moody](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- 1. retry with exponential backoff ----------------------------------------
            // Only timeouts are retried, and every backoff wait is cancellation-aware.
            const qb::retry_policy policy{
                .max_attempts = 4, .backoff = qb::duration{20ms}, .multiplier = 2.0, .max_backoff = qb::duration{200ms}, .jitter = 0.2
            };
            auto t0 = ctx.time();
            Call req;
            req.arg   = 21;
            auto done = co_await qb::ask_retry(ctx, flaky, req, qb::duration{60ms}, policy);
            qb::io::cout() << "[retry] attempt 3 of at most 4 succeeded after " << ms_since(ctx, t0) << " ms, answer " << done.response
                           << " (two timeouts, two backoffs, one line of caller code)\n";

            // ---- 2. a circuit breaker in front of a dependency that is down ----------------
            auto breaker = std::make_shared<qb::CircuitBreaker>(2u, qb::duration{150ms});
            for (int i = 1; i <= 2; ++i) {
                try {
                    (void) co_await qb::ask_guarded(ctx, breaker, moody, Call{}, qb::duration{60ms});
                } catch (qb::io::async::timeout_error const &) {
                    qb::io::cout() << "[breaker] call " << i << " timed out; failures=" << breaker->failure_count()
                                   << " state=" << state_name(breaker->state()) << "\n";
                }
            }
            t0 = ctx.time();
            try {
                (void) co_await qb::ask_guarded(ctx, breaker, moody, Call{}, qb::duration{60ms});
            } catch (qb::circuit_open_error const &) {
                qb::io::cout() << "[breaker] 2 failures tripped it; the 3rd call threw circuit_open_error in " << ms_since(ctx, t0)
                               << " ms instead of waiting for a timeout\n";
            }
            // The dependency recovers; the breaker does not know that and must not be told —
            // it finds out by admitting exactly one trial once the cooldown has elapsed.
            ctx.push_to<Recover>(moody);
            co_await ctx.sleep(200ms); // past the 150 ms cooldown
            auto ok = co_await qb::ask_guarded(ctx, breaker, moody, Call{}, qb::duration{200ms});
            (void) ok;
            qb::io::cout() << "[breaker] after the cooldown a half-open trial succeeded; state=" << state_name(breaker->state()) << "\n";

            // ---- 3. a token bucket in front of yourself -----------------------------------
            auto limiter = std::make_shared<qb::rate_limiter>(2.0, qb::duration{25ms});
            t0           = ctx.time();
            for (int i = 0; i < 6; ++i)
                co_await limiter->acquire(ctx); // first two immediate, then one per 25 ms
            qb::io::cout() << "[rate_limiter] 6 acquires, burst 2 then 1 per 25 ms: " << ms_since(ctx, t0)
                           << " ms — throttled, and not one call dropped\n";

            // ---- 4. a bulkhead: cap what is in flight, isolate the blast radius ------------
            auto                                   bh   = std::make_shared<qb::bulkhead>(2);
            auto                                   peak = std::make_shared<Peak>();
            std::vector<qb::io::async::task<void>> ops;
            for (int i = 0; i < 5; ++i)
                ops.emplace_back(guarded_op(ctx, bh, peak));
            co_await qb::io::async::when_all(std::move(ops));
            qb::io::cout() << "[bulkhead] 5 operations, cap 2: peak concurrency " << peak->max << ", every operation ran, none rejected ("
                           << bh->available() << " slots free again)\n";

            qb::io::cout() << "=== resilience complete: four policies, none of them hand-rolled ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    void
    on(Call &e) {
        (void) resolve_ask(e);
    }

private:
    static std::uint64_t
    ms_since(qb::ScopedCoroContext const &ctx, std::uint64_t t0) {
        return (ctx.time() - t0) / 1'000'000ull;
    }
};

int
main() {
    qb::Main engine;

    auto flaky = engine.addActor<FlakyService>(0, 2); // swallows the first two requests
    auto moody = engine.addActor<MoodyService>(0);    // down until it is told to recover
    engine.addActor<Caller>(0, flaky, moody);

    qb::io::cout() << "[main] one flaky service and one that is down; four policies over them\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
