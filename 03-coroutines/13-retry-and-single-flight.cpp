/**
 * @file examples/03-coroutines/13-retry-and-single-flight.cpp
 * @tier 03-coroutines
 * @teaches One flaky operation and several callers who all want it: `with_retry` decides when
 *          to try again and when to give up, and `shared_task` makes five callers share ONE
 *          attempt instead of each starting their own. Together they are the difference between
 *          a retry that heals a blip and a retry that becomes the outage.
 * @demonstrates qb::io::async::with_retry, qb::io::async::with_retry_until,
 *               qb::io::async::retry, qb::io::async::make_retryable,
 *               qb::io::async::retry_policy, qb::io::async::backoff_strategy,
 *               qb::io::async::retry_exhausted, qb::io::async::transient_network_policy,
 *               qb::io::async::idempotent_policy, qb::io::async::aggressive_retry_policy,
 *               qb::io::async::shared_task<int>, qb::io::async::make_shared_task, is_ready,
 *               valid, attempts, rethrow_last, qb::io::async::coroutine_scope,
 *               qb::io::async::task<int>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/07-structured-concurrency, 03-coroutines/08-bounded-fan-out
 * @expect "[with_retry] the call failed twice and succeeded on attempt 3"
 * @expect "[with_retry] a non-retryable error is rethrown AS ITSELF"
 * @expect "[with_retry] exhausting the budget throws retry_exhausted"
 * @expect "[backoff] fixed / linear / exponential over the same 4 attempts waited at least"
 * @expect "[with_retry_until] retried on a RESULT, not on an exception"
 * @expect "[make_retryable] wrapped the callable once and reused it"
 * @expect "[stampede] 5 independent callers made 5 calls"
 * @expect "[shared_task] the same 5 callers made 1 call"
 * @expect "[shared_task] a late joiner got the cached result without re-running anything"
 * @expect "=== retry and single flight complete: 5 calls became 1 ==="
 *
 * TWO HALVES OF ONE PROBLEM
 * -------------------------
 * A remote call fails sometimes. The obvious fix — try again — is correct and incomplete,
 * because the interesting failure is not "one call failed", it is "the service got slow, so
 * every caller retried, so the service got slower". This file builds both halves:
 *
 *   with_retry(f, policy)   turns a transient failure into a success, with a BACKOFF so the
 *                           retries spread out instead of arriving together, an `is_retryable`
 *                           predicate so a 404 is not retried like a timeout, and a hard budget
 *                           after which it raises `retry_exhausted` rather than trying forever.
 *
 *   make_shared_task(t)     starts ONE attempt and hands out a copyable handle. Five coroutines
 *                           awaiting the same handle produce one call, not five — and a sixth
 *                           that arrives after it finished gets the cached result with no work
 *                           at all.
 *
 * Put together: five callers, one in-flight attempt, retried three times behind them. §5
 * measures 5 calls without the handle and 1 with it, from the same five callers, and that pair
 * of numbers is what the summary is gated on.
 *
 * `retry_policy` IS AN AGGREGATE, so designated initialisers work and the fields you do not
 * name keep their defaults (3 attempts, 100 ms base, 30 s cap, exponential).
 *
 * THREE UNRELATED THINGS IN THIS TREE ARE CALLED RETRY. This file is the generic qb-io one,
 * which wraps any awaitable and needs neither an actor nor a module. `qb::retry_policy` +
 * `qb::ask_retry` (an actor asking another actor) live in `04-patterns/05-resilience`, and
 * `qb::redis::RetryPolicy` + `connect_with_retry` in `06-modules/redis/09-reliability`.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-retry-and-single-flight
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-retry-and-single-flight
 */

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// The two measured call counts: five callers without a shared handle, and with one.
struct Calls {
    int independent = -1;
    int shared      = -1;
};
using Meter = std::shared_ptr<Calls>;

// A distinguishable failure, so `is_retryable` has something to discriminate on.
struct permanent_error : std::runtime_error {
    permanent_error()
        : std::runtime_error("404 not found") {}
};

/**
 * The flaky remote call. It fails `fail_times` times and then succeeds, and it counts every
 * attempt — so "how many times did we actually call the service" is measured rather than
 * inferred. Parameters BY VALUE, as every coroutine here takes them.
 */
task<int>
flaky_fetch(std::shared_ptr<int> attempts, int fail_times) {
    ++(*attempts);
    co_await sleep(5ms);
    if (*attempts <= fail_times)
        throw std::runtime_error("connection reset by peer");
    co_return 200;
}

// ---------------------------------------------------------------------------------------
// 1. with_retry — the happy path, the non-retryable path, and the exhausted path.
// ---------------------------------------------------------------------------------------
task<void>
demo_with_retry() {
    qb::io::cout() << "-- 1. with_retry: try again, but not forever and not for everything\n";

    // The callable is invoked afresh on every attempt, which is why it is a FACTORY and not a
    // task: a task<T> is a one-shot coroutine and could not be awaited twice.
    auto attempts = std::make_shared<int>(0);
    auto seen     = std::make_shared<std::vector<size_t>>();

    retry_policy policy{
        .max_attempts = 4,
        .base_delay   = 10ms,
        .strategy     = backoff_strategy::fixed,
    };
    // `on_retry` fires only when a retry actually FOLLOWS, so it reports max_attempts-1 at most
    // — the failure that exhausts the budget is permanent and is not a "retrying" event.
    policy.on_retry = [seen](size_t attempt, const std::exception &) {
        seen->push_back(attempt);
    };

    const int code = co_await with_retry([attempts]() -> task<int> { return flaky_fetch(attempts, 2); }, policy);

    if (code == 200 && *attempts == 3 && seen->size() == 2)
        qb::io::cout() << "[with_retry] the call failed twice and succeeded on attempt 3, and on_retry fired "
                          "twice — once per retry that actually followed\n";
    else
        qb::io::cout() << "[with_retry] UNEXPECTED: code " << code << " after " << *attempts << " attempts, " << seen->size()
                       << " notifications\n";

    // `is_retryable` is what stops a retry loop from hammering a service over an error that
    // will never change. A false answer rethrows the ORIGINAL exception, not retry_exhausted,
    // so the caller sees the real cause.
    retry_policy discriminating;
    discriminating.max_attempts = 5;
    discriminating.base_delay   = 5ms;
    discriminating.is_retryable = [](const std::exception &e) {
        return std::string{e.what()}.find("404") == std::string::npos;
    };

    auto never = std::make_shared<int>(0);
    try {
        co_await with_retry(
            [never]() -> task<int> {
                ++(*never);
                co_await sleep(1ms);
                throw permanent_error();
            },
            discriminating);
        qb::io::cout() << "[with_retry] UNEXPECTED: the permanent error was swallowed\n";
    } catch (const permanent_error &e) {
        if (*never == 1)
            qb::io::cout() << "[with_retry] a non-retryable error is rethrown AS ITSELF after exactly one attempt "
                              "— not wrapped, not retried, so the caller sees the real cause\n";
        else
            qb::io::cout() << "[with_retry] UNEXPECTED: " << *never << " attempts for a non-retryable error (" << e.what() << ")\n";
    }

    // And the budget. `retry_exhausted` carries both the count and the last error.
    auto hopeless = std::make_shared<int>(0);
    try {
        co_await with_retry([hopeless]() -> task<int> { return flaky_fetch(hopeless, 99); },
                            retry_policy{.max_attempts = 3, .base_delay = 5ms, .strategy = backoff_strategy::fixed});
        qb::io::cout() << "[with_retry] UNEXPECTED: the hopeless call returned\n";
    } catch (const retry_exhausted &ex) {
        std::string last;
        try {
            ex.rethrow_last();
        } catch (const std::exception &inner) {
            last = inner.what();
        }
        if (ex.attempts() == 3 && *hopeless == 3 && last == "connection reset by peer")
            qb::io::cout() << "[with_retry] exhausting the budget throws retry_exhausted, and it carries attempts()=3 "
                              "plus rethrow_last() = the real error underneath\n\n";
        else
            qb::io::cout() << "[with_retry] UNEXPECTED: attempts()=" << ex.attempts() << ", calls " << *hopeless << ", last \"" << last
                           << "\"\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 2. Backoff — the part that decides whether your retry helps or hurts.
// ---------------------------------------------------------------------------------------
task<long long>
time_exhaustion(backoff_strategy strategy) {
    auto       calls = std::make_shared<int>(0);
    const auto t0    = std::chrono::steady_clock::now();
    try {
        co_await with_retry([calls]() -> task<int> { return flaky_fetch(calls, 99); },
                            retry_policy{.max_attempts = 4, .base_delay = 20ms, .max_delay = 5s, .strategy = strategy});
    } catch (const retry_exhausted &) {
        // expected: this measures the WAITING, not the outcome
    }
    co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

task<void>
demo_backoff() {
    qb::io::cout() << "-- 2. backoff: four attempts, three gaps, three shapes\n";

    // Same budget, same base delay, three strategies. With base 20 ms and three gaps:
    //   fixed        20 + 20 + 20  =  60 ms
    //   linear       20 + 40 + 60  = 120 ms
    //   exponential  20 + 40 + 80  = 140 ms
    // plus the four 5 ms calls themselves. Exponential is the default, and it is the default
    // because it is the one that gets OUT of a busy service's way.
    const auto fixed_ms = co_await time_exhaustion(backoff_strategy::fixed);
    const auto lin_ms   = co_await time_exhaustion(backoff_strategy::linear);
    const auto exp_ms   = co_await time_exhaustion(backoff_strategy::exponential);

    // Gated on the LOWER bounds each strategy guarantees (60 / 120 / 140 ms of sleeping), not
    // on the three measurements out-racing each other: a lower bound cannot be broken by a
    // machine being slow, and the bounds themselves are already strictly increasing.
    if (fixed_ms >= 60 && lin_ms >= 120 && exp_ms >= 140)
        qb::io::cout() << "[backoff] fixed / linear / exponential over the same 4 attempts waited at least "
                          "60 / 120 / 140 ms — the exponential one is the default for exactly that reason\n";
    else
        qb::io::cout() << "[backoff] UNEXPECTED: " << fixed_ms << ", " << lin_ms << ", " << exp_ms << " ms\n";
    qb::io::cout() << "    measured " << fixed_ms << " / " << lin_ms << " / " << exp_ms << " ms; "
                   << "exponential_jitter adds randomness so a thundering herd does not re-converge\n";

    // The three named policies are ready-made retry_policy values, not new machinery.
    const auto transient  = transient_network_policy();
    const auto idempotent = idempotent_policy();
    const auto aggressive = aggressive_retry_policy();
    qb::io::cout() << "    ready-made: transient_network_policy() " << transient.max_attempts << " attempts, idempotent_policy() "
                   << idempotent.max_attempts << ", aggressive_retry_policy() " << aggressive.max_attempts << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 3. with_retry_until / retry / make_retryable.
// ---------------------------------------------------------------------------------------
task<int>
slowly_converging(std::shared_ptr<int> polls) {
    ++(*polls);
    co_await sleep(5ms);
    co_return *polls * 25; // 25, 50, 75, 100 …
}

task<void>
demo_variants() {
    qb::io::cout() << "-- 3. the three other entry points\n";

    // `with_retry_until` retries on a RESULT rather than on an exception — the shape you need
    // for "poll until the job reports done", where nothing ever throws.
    auto       polls = std::make_shared<int>(0);
    const auto ripe  = co_await with_retry_until([polls]() -> task<int> { return slowly_converging(polls); }, [](int v) { return v >= 100; },
                                                 retry_policy{.max_attempts = 8, .base_delay = 5ms, .strategy = backoff_strategy::fixed});
    if (ripe == 100 && *polls == 4)
        qb::io::cout() << "[with_retry_until] retried on a RESULT, not on an exception: four polls until the "
                          "predicate was satisfied, and nothing ever threw\n";
    else
        qb::io::cout() << "[with_retry_until] UNEXPECTED: " << ripe << " after " << *polls << " polls\n";

    // `retry(f)` is `with_retry(f, {})` — the default policy, spelled shortly.
    auto      once = std::make_shared<int>(0);
    const int easy = co_await retry([once]() -> task<int> { return flaky_fetch(once, 0); });
    qb::io::cout() << "    retry(f) is with_retry(f, {}): default policy, " << *once << " attempt, result " << easy << "\n";

    // `make_retryable` binds the callable and the policy ONCE and hands back something you can
    // await repeatedly — the right shape for a member that is called from many places.
    auto      tries   = std::make_shared<int>(0);
    auto      fetcher = make_retryable([tries]() -> task<int> { return flaky_fetch(tries, 1); },
                                       retry_policy{.max_attempts = 3, .base_delay = 5ms, .strategy = backoff_strategy::fixed});
    const int a       = co_await fetcher();
    const int b       = co_await fetcher();
    if (a == 200 && b == 200)
        qb::io::cout() << "[make_retryable] wrapped the callable once and reused it: two awaits of the same "
                          "wrapper, each with its own retry budget\n\n";
    else
        qb::io::cout() << "[make_retryable] UNEXPECTED: " << a << ", " << b << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 4 + 5. The stampede, and shared_task as the fix.
// ---------------------------------------------------------------------------------------
task<void>
independent_caller(std::shared_ptr<int> calls, std::shared_ptr<int> results) {
    // Every caller starts its OWN attempt. Correct, and five times the load.
    const int v = co_await flaky_fetch(calls, 0);
    *results += v;
}

task<void>
sharing_caller(shared_task<int> handle, std::shared_ptr<int> results) {
    // `shared_task<int>` is COPYABLE, unlike task<int>. Awaiting a copy joins the one
    // computation already in flight; it does not start another.
    const int v = co_await handle;
    *results += v;
}

task<void>
demo_single_flight(Meter meter) {
    qb::io::cout() << "-- 4. five callers, five calls\n";

    {
        auto            calls   = std::make_shared<int>(0);
        auto            results = std::make_shared<int>(0);
        coroutine_scope scope;
        for (int i = 0; i < 5; ++i)
            scope.spawn(independent_caller(calls, results));
        co_await scope.join_all();

        meter->independent = *calls;
        if (*calls == 5 && *results == 1000)
            qb::io::cout() << "[stampede] 5 independent callers made 5 calls for one piece of information — "
                              "which is the shape that turns a slow service into a down one\n\n";
        else
            qb::io::cout() << "[stampede] UNEXPECTED: " << *calls << " calls, total " << *results << "\n\n";
    }

    qb::io::cout() << "-- 5. shared_task: the same five callers, one call\n";

    {
        auto calls   = std::make_shared<int>(0);
        auto results = std::make_shared<int>(0);

        // `make_shared_task` STARTS the work now and returns a handle. Note the difference from
        // a task<T>, which does not begin until awaited: there is one attempt in flight from
        // this line onwards, and every await below joins it.
        auto handle = make_shared_task(flaky_fetch(calls, 0));
        if (!handle.valid())
            qb::io::cout() << "    UNEXPECTED: the handle is not valid\n";

        coroutine_scope scope;
        for (int i = 0; i < 5; ++i)
            scope.spawn(sharing_caller(handle, results));
        co_await scope.join_all();

        meter->shared = *calls;
        if (*calls == 1 && *results == 1000)
            qb::io::cout() << "[shared_task] the same 5 callers made 1 call and all five got 200 — the copies of "
                              "the handle are copies of the RESULT, not of the work\n";
        else
            qb::io::cout() << "[shared_task] UNEXPECTED: " << *calls << " calls, total " << *results << "\n";

        // A late joiner costs nothing at all: the result is cached in the shared state, so the
        // await completes without suspending. `is_ready()` is how you can tell in advance.
        const bool ready = handle.is_ready();
        const int  late  = co_await handle;
        if (ready && late == 200 && *calls == 1)
            qb::io::cout() << "[shared_task] a late joiner got the cached result without re-running anything, and "
                              "is_ready() said so before the await\n\n";
        else
            qb::io::cout() << "[shared_task] UNEXPECTED late join: ready=" << ready << ", value " << late << ", calls " << *calls << "\n\n";
    }
}

task<void>
run_all(Meter meter) {
    co_await demo_with_retry();
    co_await demo_backoff();
    co_await demo_variants();
    co_await demo_single_flight(meter);
}

int
main() {
    qb::io::cout() << "=== retry and single flight: one flaky call, several callers ===\n\n";

    init();

    auto meter = std::make_shared<Calls>();
    run_sync(run_all(meter));

    // Gated on the two measured call counts, because they are the claim.
    qb::io::cout() << "    measured: " << meter->independent << " calls from 5 independent callers, " << meter->shared
                   << " from the same 5 sharing one handle\n";
    if (meter->independent == 5 && meter->shared == 1)
        qb::io::cout() << "=== retry and single flight complete: 5 calls became 1 ===\n";
    else
        qb::io::cout() << "=== retry and single flight INCONCLUSIVE: the call counts are not 5 and 1 ===\n";
    return 0;
}
