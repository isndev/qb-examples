/**
 * @file examples/03-coroutines/15-offloading-blocking-work.cpp
 * @tier 03-coroutines
 * @teaches A call that blocks -- a cold file, a DNS lookup, a deliberately slow KDF -- freezes
 *          every coroutine, timer and socket of its loop for as long as it runs. `co_await
 *          offload(fn, args...)` runs it on a small pool instead and resumes on the loop, which
 *          keeps turning meanwhile. Inside an actor, `ctx.offload(...)` adds what a kill needs:
 *          the wait ends at once, and the call's late result is discarded.
 * @demonstrates qb::io::async::offload, qb::io::async::set_offload_threads,
 *               qb::io::async::current_offload_stats, qb::io::async::offload_stats,
 *               ctx.offload, qb::ScopedCoroContext, qb::io::async::cancelled_error,
 *               qb::io::async::run_sync, qb::io::async::when_all, qb::io::async::sleep
 * @prerequisites 03-coroutines/01-first-coroutine, 03-coroutines/02-actor-coroutines
 * @expect "[inline] the blocking call froze the loop: the heartbeat stopped for as long as it ran"
 * @expect "[offload] the loop kept its heartbeat while a pool thread ran the same call"
 * @expect "[offload] the exception thrown on the pool was caught on the loop"
 * @expect "[ctx.offload] the kill ended the wait before the call returned"
 * @expect "[ctx.offload] the call's late result was discarded on the core, handed to no one"
 * @expect "=== offloading complete ==="
 *
 * THE PROBLEM
 * -----------
 * A qb-io loop is one thread. A coroutine that calls something slow does not suspend -- it
 * BLOCKS, and with it everything else the loop runs: here a heartbeat that ticks every 5 ms. Part
 * one makes a 300 ms blocking call twice, once inline and once through `offload`, and measures the
 * heartbeat's worst gap each time. Inline, the gap is the call. Offloaded, it stays a tick.
 *
 * THE RULES OF `offload` (qb/io/async/coroutine/offload.h)
 * -------------------------------------------------------
 *   - values in, values out: the callable and its arguments are copied at the call, run on a pool
 *     thread and destroyed there; the result is a value, handed back on the loop;
 *   - the callable runs on ANOTHER thread: it touches nothing of the loop -- no actor, no qb-io
 *     object, no coroutine;
 *   - the coroutine resumes on the thread that awaited, and an exception thrown on the pool is
 *     rethrown by `co_await`;
 *   - a running call cannot be interrupted.
 *
 * THE ACTOR HALF
 * --------------
 * That last rule is why actors get their own spelling. An actor's coroutine is not destroyed when
 * the actor is killed: its context's operations are woken with `cancelled_error`, everything else
 * waits on. `ctx.offload` is one of those operations -- part two kills an actor while its call is
 * still running, and the wait ends at once; the call finishes on the pool, and its result is
 * destroyed on the core, counted in `offload_stats::discarded` and handed to no one. The bare
 * `qb::io::async::offload` would have kept the coroutine waiting through the kill, and resumed it
 * afterwards.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-offloading-blocking-work
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-offloading-blocking-work
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/main.h>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

constexpr auto kBlock = 300ms; ///< how long the blocking call blocks
constexpr auto kTick  = 5ms;   ///< the heartbeat's period

/// Stands for any call that blocks the thread it runs on: a read of a cold file, `getaddrinfo`,
/// a KDF sized for a login endpoint.
int
blocking_call(int x) {
    std::this_thread::sleep_for(kBlock);
    return x * 2;
}

long long
ms(Clock::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

// ---- part one: the same call, inline and offloaded, beside a heartbeat ----------------------

/// Ticks every `kTick` until `stop`, and returns the WORST gap it saw between two ticks.
qb::io::async::task<Clock::duration>
heartbeat(std::shared_ptr<std::atomic<bool>> stop) {
    Clock::duration worst{};
    auto            last = Clock::now();
    while (!stop->load()) {
        co_await qb::io::async::sleep(kTick);
        const auto now = Clock::now();
        worst          = std::max(worst, now - last);
        last           = now;
    }
    co_return worst;
}

/// The call made ON the loop thread: nothing else of the loop runs until it returns.
qb::io::async::task<int>
call_inline(std::shared_ptr<std::atomic<bool>> stop) {
    co_await qb::io::async::sleep(20ms); // let the heartbeat settle
    const int r = blocking_call(21);
    co_await qb::io::async::sleep(20ms);
    stop->store(true);
    co_return r;
}

/// The same call through `offload`: a pool thread blocks, this coroutine is suspended, the loop runs.
qb::io::async::task<int>
call_offloaded(std::shared_ptr<std::atomic<bool>> stop) {
    co_await qb::io::async::sleep(20ms);
    const int r = co_await qb::io::async::offload(blocking_call, 21);
    co_await qb::io::async::sleep(20ms);
    stop->store(true);
    co_return r;
}

/// An exception thrown by the call, on the pool, is rethrown by `co_await`, on the loop.
qb::io::async::task<bool>
exception_crosses() {
    try {
        (void) co_await qb::io::async::offload([]() -> int { throw std::runtime_error("disk unreadable"); });
    } catch (std::runtime_error const &e) {
        co_return std::string{e.what()} == "disk unreadable";
    }
    co_return false;
}

// ---- part two: an actor killed while its call runs ----------------------------------------

std::atomic<bool>          g_call_started{false};
std::atomic<bool>          g_call_returned{false};
std::atomic<bool>          g_wait_ended_early{false};
std::atomic<bool>          g_ran_on{false};
std::atomic<std::uint64_t> g_discarded_before{0};

/// Offloads the blocking call and kills itself as soon as the call has started.
class Hasher
    : public qb::Actor
    , public qb::ICallback {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerCallback(*this);
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            try {
                (void) co_await ctx.offload([] {
                    g_call_started  = true;
                    const int r     = blocking_call(1);
                    g_call_returned = true;
                    return r;
                });
                g_ran_on = true; // never: the actor was killed while the call ran
            } catch (qb::io::async::cancelled_error const &) {
                g_wait_ended_early = !g_call_returned.load(); // the wait ended BEFORE the call did
            }
        });
        co_return true;
    }
    void
    on(qb::LoopEvent const &) override {
        if (g_call_started.load())
            kill();
    }
};

/// Keeps the core alive until the killed actor's late result has been discarded on it.
class Witness
    : public qb::Actor
    , public qb::ICallback {
    Clock::time_point _deadline = Clock::now() + 10s;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerCallback(*this);
        co_return true;
    }
    void
    on(qb::LoopEvent const &) override {
        if (qb::io::async::current_offload_stats().discarded > g_discarded_before.load() || Clock::now() > _deadline)
            kill();
    }
};

} // namespace

int
main() {
    std::cout << "=== offloading blocking work ===" << std::endl;
    // The pool starts with the first offload; its size can be set only before that.
    (void) qb::io::async::set_offload_threads(2);

    // Part one.
    {
        auto stop         = std::make_shared<std::atomic<bool>>(false);
        auto [gap, value] = qb::io::async::run_sync(qb::io::async::when_all(heartbeat(stop), call_inline(stop)));
        std::cout << "inline:    result " << value << ", worst heartbeat gap " << ms(gap) << " ms" << std::endl;
        if (value == 42 && gap >= kBlock * 3 / 4)
            std::cout << "[inline] the blocking call froze the loop: the heartbeat stopped for as long as it ran" << std::endl;
    }
    {
        auto stop         = std::make_shared<std::atomic<bool>>(false);
        auto [gap, value] = qb::io::async::run_sync(qb::io::async::when_all(heartbeat(stop), call_offloaded(stop)));
        std::cout << "offloaded: result " << value << ", worst heartbeat gap " << ms(gap) << " ms" << std::endl;
        if (value == 42 && gap < kBlock / 3)
            std::cout << "[offload] the loop kept its heartbeat while a pool thread ran the same call" << std::endl;
    }
    if (qb::io::async::run_sync(exception_crosses()))
        std::cout << "[offload] the exception thrown on the pool was caught on the loop" << std::endl;

    // Part two.
    g_discarded_before = qb::io::async::current_offload_stats().discarded;
    qb::Main engine;
    engine.addActor<Hasher>(0);
    engine.addActor<Witness>(0);
    engine.start();
    engine.join();
    if (engine.hasError())
        return 1;
    if (g_wait_ended_early.load() && !g_ran_on.load())
        std::cout << "[ctx.offload] the kill ended the wait before the call returned" << std::endl;
    if (qb::io::async::current_offload_stats().discarded == g_discarded_before.load() + 1)
        std::cout << "[ctx.offload] the call's late result was discarded on the core, handed to no one" << std::endl;

    const qb::io::async::offload_stats s = qb::io::async::current_offload_stats();
    std::cout << "pool: " << s.threads << " threads, " << s.submitted << " submitted, " << s.completed << " completed, " << s.discarded
              << " discarded" << std::endl;
    std::cout << "=== offloading complete ===" << std::endl;
    return 0;
}
