/**
 * @file examples/03-coroutines/08-bounded-fan-out.cpp
 * @tier 03-coroutines
 * @teaches How many pieces of work are allowed to be in flight AT ONCE — all of them
 *          (`parallel`), exactly K of them (`parallel_map`), or one (`repeat_while`). The
 *          middle one is the answer almost every real system needs, and this program measures
 *          the ceiling rather than trusting it.
 * @demonstrates qb::io::async::parallel, qb::io::async::parallel_map,
 *               qb::io::async::repeat_while, qb::io::async::capture_result,
 *               qb::io::async::coroutine_scope, qb::io::async::cancellation_token,
 *               qb::io::async::task<int>, qb::io::async::task<void>,
 *               qb::io::async::task<std::string>, qb::io::async::sleep,
 *               qb::io::async::run_sync
 * @prerequisites 03-coroutines/05-combinators, 03-coroutines/07-structured-concurrency
 * @expect "[parallel] tuple came back in ARGUMENT order"
 * @expect "[parallel] the task<void> branch occupies a std::monostate slot"
 * @expect "[parallel_map] 12 items, no limit: peak in flight was 12"
 * @expect "[parallel_map] 12 items, max_concurrency=3: peak in flight was 3"
 * @expect "[parallel_map] both runs returned 12 results, in INPUT order"
 * @expect "[repeat_while] ran 5 times, one at a time"
 * @expect "[repeat_while] a pre-cancelled token means ZERO iterations"
 * @expect "[capture_result] the scope wrote 7 into the optional"
 * @expect "=== bounded fan-out complete: peak 12 then peak 3, from the same 12 items ==="
 *
 * WHY A LIMIT IS THE INTERESTING CASE
 * -----------------------------------
 * `when_all` and `parallel` start every branch immediately. That is correct, and it is what
 * you want for three branches. For twelve hundred it is a denial of service you wrote
 * yourself: a connection pool with ten slots, an API with a rate limit, a disk with a queue
 * depth. The shape you actually need is "all of them, K at a time", and `parallel_map` is it —
 * its third argument builds a `semaphore` every worker must pass through, so the ceiling is
 * enforced by the primitive rather than by the caller's good manners.
 *
 * WHAT THIS PROGRAM MEASURES
 * --------------------------
 * A gauge is incremented when a worker begins its body and decremented when it ends, and the
 * high-water mark is kept. The same twelve items are mapped twice, the only difference being
 * `max_concurrency`. Both peaks are printed and the summary is gated on them, so this is a
 * measurement of the running program rather than a restatement of the documentation.
 *
 * A LIMIT HERE IS ABOUT RESOURCES, NOT CORES. Every coroutine in this file runs on ONE thread
 * and they interleave only at suspension points. "Three at a time" means three sockets open,
 * three rows locked, three requests outstanding — never three CPUs busy.
 *
 * THREE THINGS THAT LOOK LIKE BUGS AND ARE NOT
 * --------------------------------------------
 *   1. `parallel(...)` returns a TUPLE in argument order, never in completion order. So does
 *      `when_all`. If you need to know who finished first, that is `when_any` / `join_any`.
 *   2. A `task<void>` branch cannot store "nothing", so it occupies a `std::monostate` slot in
 *      the tuple. `parallel`, `parallel_map` and `when_all` all share that rule.
 *   3. `parallel_map` returns results in INPUT order even though the workers finish out of
 *      order — each worker writes to its own index, it never push_backs.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-bounded-fan-out
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-bounded-fan-out
 */

#include <chrono>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// THE INSTRUMENT. `in_flight` is incremented at the top of a worker's body and decremented at
// the bottom; `peak` is the high-water mark. A plain int is the honest type: these coroutines
// all run on one thread.
struct Gauge {
    int in_flight = 0;
    int peak      = 0;

    void
    enter() {
        if (++in_flight > peak)
            peak = in_flight;
    }
    void
    leave() {
        --in_flight;
    }
};

// The two measured peaks, carried out of the demo and into main()'s gate. A shared_ptr rather
// than a reference into a caller frame, because a worker's frame can outlive the statement
// that spawned it.
struct Peaks {
    int wide   = -1;
    int narrow = -1;
};
using Report = std::shared_ptr<Peaks>;

// ---------------------------------------------------------------------------------------
// 1. parallel — every branch at once, results as a tuple in ARGUMENT order.
// ---------------------------------------------------------------------------------------
task<int>
priced(int cents, qb::duration cost) {
    co_await sleep(cost);
    co_return cents;
}

task<std::string>
named(std::string who, qb::duration cost) {
    co_await sleep(cost);
    co_return who;
}

task<void>
side_effect_only(qb::duration cost) {
    co_await sleep(cost);
}

task<void>
demo_parallel() {
    qb::io::cout() << "-- 1. parallel: every branch at once, heterogeneous results\n";

    // The slowest branch is 90 ms and the three sum to 150 ms; `parallel` costs the former.
    // Branches are taken BY VALUE, so each argument must be a prvalue (or std::move'd).
    const auto t0              = std::chrono::steady_clock::now();
    auto [cents, who, nothing] = co_await parallel(priced(1999, 30ms), named("bordeaux", 90ms), side_effect_only(30ms));
    const auto ms              = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    qb::io::cout() << "[parallel] tuple came back in ARGUMENT order: " << cents << " and \"" << who << "\" after " << ms
                   << " ms — the SLOWEST branch, not the sum\n";

    // `nothing` is a std::monostate: `void` is not a storable type, so the slot exists to keep
    // the tuple positions lined up with the arguments. `when_all` and `parallel_map` agree.
    static_assert(std::is_same_v<decltype(nothing), std::monostate>, "a task<void> branch stores std::monostate");
    qb::io::cout() << "[parallel] the task<void> branch occupies a std::monostate slot — `void` is not storable, "
                      "and dropping the slot would silently renumber every branch after it\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. parallel_map — the same items, twice, with and without a ceiling.
// ---------------------------------------------------------------------------------------
task<int>
fetch_row(int id, std::shared_ptr<Gauge> meter) {
    meter->enter();
    co_await sleep(30ms); // stand-in for the thing that is actually rate-limited
    meter->leave();
    co_return id * 10;
}

task<void>
demo_parallel_map(Report report) {
    qb::io::cout() << "-- 2. parallel_map: the same 12 items, twice\n";

    std::vector<int> items(12);
    std::iota(items.begin(), items.end(), 1); // 1..12

    // -- (a) no ceiling: max_concurrency at least as large as the input. -----------------
    auto       wide    = std::make_shared<Gauge>();
    const auto t0      = std::chrono::steady_clock::now();
    auto       all     = co_await parallel_map(items, [wide](int id) -> task<int> { co_return co_await fetch_row(id, wide); }, 12);
    const auto wide_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    qb::io::cout() << "    wall clock " << wide_ms << " ms — one 30 ms wait, twelve times over\n";
    if (wide->peak == 12)
        qb::io::cout() << "[parallel_map] 12 items, no limit: peak in flight was 12 — every worker began "
                          "before any of them finished\n";
    else
        qb::io::cout() << "[parallel_map] UNEXPECTED: unlimited peak was " << wide->peak << ", not 12\n";

    // -- (b) ceiling of 3. Same items, same work, one argument different. ------------------
    auto       narrow    = std::make_shared<Gauge>();
    const auto t1        = std::chrono::steady_clock::now();
    auto       limited   = co_await parallel_map(items, [narrow](int id) -> task<int> { co_return co_await fetch_row(id, narrow); }, 3);
    const auto narrow_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();

    qb::io::cout() << "    wall clock " << narrow_ms << " ms — four waves of three\n";
    if (narrow->peak == 3)
        qb::io::cout() << "[parallel_map] 12 items, max_concurrency=3: peak in flight was 3 — a fourth worker "
                          "PARKS on the semaphore rather than proceeding and hoping\n";
    else
        qb::io::cout() << "[parallel_map] UNEXPECTED: limited peak was " << narrow->peak << ", not 3\n";

    // Results are indexed, not appended: worker i writes slot i regardless of when it finishes.
    bool ordered = all.size() == 12 && limited.size() == 12;
    for (size_t i = 0; ordered && i < all.size(); ++i)
        ordered = (all[i] == static_cast<int>(i + 1) * 10) && (limited[i] == all[i]);
    if (ordered)
        qb::io::cout() << "[parallel_map] both runs returned 12 results, in INPUT order (" << all.front() << ".." << all.back()
                       << ") — a worker writes ITS index\n\n";
    else
        qb::io::cout() << "[parallel_map] UNEXPECTED: the results were not the 12 values in input order\n\n";

    report->wide   = wide->peak;
    report->narrow = narrow->peak;
}

// ---------------------------------------------------------------------------------------
// 3. repeat_while — concurrency of ONE, on purpose.
// ---------------------------------------------------------------------------------------
task<void>
demo_repeat_while() {
    qb::io::cout() << "-- 3. repeat_while: the sequential end of the same axis\n";

    // The factory is called again only after the previous task has finished, so this is a loop
    // with a suspension in it — not a fan-out. Reach for it when the work must NOT overlap: a
    // poll, a cursor, one step of a state machine.
    int  iterations = 0;
    auto body       = [&iterations]() -> task<void> {
        ++iterations;
        co_await sleep(5ms);
    };
    co_await repeat_while(body, [&iterations]() { return iterations < 5; });

    qb::io::cout() << "[repeat_while] ran 5 times, one at a time — measured " << iterations << "\n";

    // The predicate is checked BEFORE each iteration, and so is the token. A token that is
    // already cancelled means the body never runs even once, which is the right answer for a
    // "start polling" that arrives after "stop".
    int  never      = 0;
    auto never_body = [&never]() -> task<void> {
        ++never;
        co_return;
    };
    cancellation_token token;
    token.cancel();
    co_await repeat_while(never_body, [&never]() { return never < 10; }, token);

    qb::io::cout() << "[repeat_while] a pre-cancelled token means ZERO iterations, measured " << never
                   << " — the token is tested before the factory is even called\n\n";
}

// ---------------------------------------------------------------------------------------
// 4. capture_result — the manual version of what parallel does for you.
// ---------------------------------------------------------------------------------------
task<void>
demo_capture_result() {
    qb::io::cout() << "-- 4. capture_result: getting a value out of a scope-spawned task\n";

    // `coroutine_scope::spawn` returns void, so a task spawned into a scope has nowhere to put
    // its result. `capture_result(t, slot)` is the adapter: it awaits `t` and writes into your
    // optional. It is exactly what `parallel()` does internally, once per branch.
    std::optional<int> slot;
    {
        coroutine_scope scope;
        scope.spawn(capture_result(priced(7, 20ms), slot));
        co_await scope.join_all();
    }

    if (slot.has_value() && *slot == 7)
        qb::io::cout() << "[capture_result] the scope wrote 7 into the optional — and this is why parallel()'s "
                          "own slots are shared_ptr: a reclaimed coordinator must not leave a worker writing "
                          "through a freed frame\n\n";
    else
        qb::io::cout() << "[capture_result] UNEXPECTED: the optional was not filled with 7\n\n";
}

task<void>
run_all(Report report) {
    co_await demo_parallel();
    co_await demo_parallel_map(report);
    co_await demo_repeat_while();
    co_await demo_capture_result();
}

int
main() {
    qb::io::cout() << "=== bounded fan-out: all at once, K at a time, or one at a time ===\n\n";

    init();

    auto report = std::make_shared<Peaks>();
    run_sync(run_all(report));

    // Gated on the two MEASURED peaks, not on the program having reached its last line: a
    // ceiling that silently stopped working would still print every other line above.
    qb::io::cout() << "    measured peaks: " << report->wide << " with no limit, " << report->narrow << " with max_concurrency=3\n";
    if (report->wide == 12 && report->narrow == 3)
        qb::io::cout() << "=== bounded fan-out complete: peak 12 then peak 3, from the same 12 items ===\n";
    else
        qb::io::cout() << "=== bounded fan-out INCONCLUSIVE: the peaks are not 12 and 3 ===\n";
    return 0;
}
