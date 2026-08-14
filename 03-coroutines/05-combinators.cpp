/**
 * @file examples/03-coroutines/05-combinators.cpp
 * @tier 03-coroutines
 * @teaches The five ways to combine awaitables — wait for ALL of them, wait for the FIRST,
 *          and bound one with a timeout or a deadline — plus the one property that decides
 *          which you should reach for: what happens to the branches that LOSE.
 * @demonstrates qb::io::async::when_all, qb::io::async::when_any,
 *               qb::io::async::when_any_result, qb::io::async::race,
 *               qb::io::async::coro_with_timeout, qb::io::async::with_deadline,
 *               qb::io::async::timeout_error, qb::io::async::task<int>,
 *               qb::io::async::task<void>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/01-first-coroutine
 * @expect "[when_all] both branches ran; results "
 * @expect "[when_any] winner was branch"
 * @expect "[when_any] the LOSER never printed: a losing branch is torn down"
 * @expect "[when_any] the vector form hands back a std::pair, not a when_any_result"
 * @expect "[coro_with_timeout] threw timeout_error after"
 * @expect "[coro_with_timeout] and the abandoned task FINISHED anyway"
 * @expect "[with_deadline] threw timeout_error after"
 * @expect "[with_deadline] and its branch was reclaimed: nothing finished behind our back"
 * @expect "=== combinators complete: 6 of 10 branches finished, 4 were reclaimed ==="
 *
 * THE ONE QUESTION THAT SEPARATES THESE FIVE
 * ------------------------------------------
 * Every combinator here answers "how do I wait for more than one thing at once". They differ
 * on something the signature does not say: when the wait ends early, what becomes of the work
 * that had not finished?
 *
 *   when_all           nothing ends early — it resumes when the LAST branch is done.
 *   when_any / race    the losers are RECLAIMED: torn down at their current suspension point.
 *   coro_with_timeout  the inner task is NOT touched. It keeps running to completion, alone,
 *                      and its result is dropped.
 *   with_deadline      built on when_any, so the branch IS reclaimed.
 *
 * That is not a detail: a losing branch that keeps running still holds its file handle, still
 * writes its row, still charges the card. This program does not assert those sentences — it
 * counts, and prints the count. Ten branches begin; six reach their own print statement and
 * four are torn down before they can.
 *
 * NO ACTOR HERE, DELIBERATELY. These are qb-io coroutine primitives; they need a loop, not an
 * engine. `run_sync` pumps the loop until the awaitable it is given completes, so the whole
 * program is main() plus a handful of `task<T>` functions.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-combinators
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-combinators
 */

#include <any>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// THE INSTRUMENT. Every combinator below starts branches, and the gap between STARTED and
// FINISHED is the entire lesson — so both are counted rather than reasoned about. A
// `shared_ptr` because a branch's frame outlives the function that created it.
struct Tally {
    int started  = 0;
    int finished = 0;
};
using Counter = std::shared_ptr<Tally>;

/**
 * One unit of work. Parameters are taken BY VALUE — a coroutine frame stores its parameters,
 * and `initial_suspend` is `suspend_always`, so the body does not begin until the scheduler
 * resumes it, by which time any caller temporary is gone. `03-coroutines/01-first-coroutine`
 * measures what a `const &` here does instead.
 *
 * The print at the end is the only evidence that matters: it happens if and only if this
 * branch was allowed to run to completion.
 */
task<int>
work(std::string label, qb::duration cost, int value, Counter tally) {
    ++tally->started; // the body really did begin — a reclaimed branch is not an unstarted one
    co_await sleep(cost);
    ++tally->finished;
    qb::io::cout() << "    ." << label << " finished (this line only prints if the branch was "
                   << "allowed to run to the end)\n";
    co_return value;
}

// A `task<void>` branch, so the `when_all` tuple below has a `std::monostate` in it and the
// reason for that is visible rather than surprising.
task<void>
tick(qb::duration cost) {
    co_await sleep(cost);
}

// ---------------------------------------------------------------------------------------
// 1. when_all — every branch, and the wait ends at the SLOWEST of them.
// ---------------------------------------------------------------------------------------
task<void>
demo_when_all(Counter tally) {
    qb::io::cout() << "-- when_all: two branches, 120 ms and 60 ms\n";
    const auto t0 = std::chrono::steady_clock::now();

    // Tasks are taken BY VALUE, so each argument must be a prvalue (or `std::move`d). The
    // result is a tuple, one slot per branch, in the order they were written — not in the
    // order they finished. A `task<void>` branch contributes `std::monostate`, which is what
    // "a slot for a value that does not exist" looks like in a tuple.
    auto res = co_await when_all(work("all-slow", 120ms, 10, tally), work("all-fast", 60ms, 20, tally), tick(30ms));

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    qb::io::cout() << "[when_all] both branches ran; results " << std::get<0>(res) << " and " << std::get<1>(res) << " after " << ms
                   << " ms — the SLOWEST branch, not the sum\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. when_any — the first to finish wins, and the rest are torn down.
// ---------------------------------------------------------------------------------------
task<void>
demo_when_any(Counter tally) {
    qb::io::cout() << "-- when_any: 40 ms against 400 ms\n";

    // `when_any_result` carries three things: `index` (which branch), `value` (a `std::any`,
    // because the branches may have different types) and `exception` (non-null when the
    // WINNER threw). Structured bindings expose exactly the first two — the arity is pinned
    // by a static_assert in the header — so read the exception with `get<T>()`, which
    // rethrows before unwrapping, or with `rethrow_if_exception()` when the winner is a
    // `task<void>` and there is nothing to unwrap.
    when_any_result r = co_await when_any(work("any-fast", 40ms, 1, tally), work("any-slow", 400ms, 2, tally));

    qb::io::cout() << "[when_any] winner was branch " << r.index << " with value " << r.get<int>() << "\n";

    // Give the slow branch far more than its 400 ms. If it were merely detached it would
    // print here. It does not: when_any destroys each loser's frame at its suspension point,
    // which stops the libev timer it was parked on.
    co_await sleep(500ms);
    qb::io::cout() << "[when_any] the LOSER never printed: a losing branch is torn down at its "
                   << "suspension point, so do not rely on its side effects\n";

    // `race(...)` is a pure alias for `when_any` — same awaiter, same reclamation. It exists
    // because "race" is what the shape is called when the branches are alternatives rather
    // than a set.
    auto rr = co_await race(work("race-a", 30ms, 7, tally), work("race-b", 300ms, 8, tally));
    qb::io::cout() << "    race() is when_any under another name; branch " << rr.index << " won\n";

    // THE TRAP. The VECTOR overload does not return a `when_any_result` — it returns a
    // `std::pair<size_t, std::any>`. Same question, different type, and `auto` hides it until
    // you reach for `.exception` and the compiler says there is no such member.
    std::vector<task<int>> lanes;
    lanes.push_back(work("vec-a", 25ms, 100, tally));
    lanes.push_back(work("vec-b", 250ms, 200, tally));
    auto [idx, val] = co_await when_any(std::move(lanes));
    qb::io::cout() << "[when_any] the vector form hands back a std::pair, not a when_any_result: "
                   << "index " << idx << ", value " << std::any_cast<int>(val) << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 3 & 4. Two ways to bound one awaitable, and they are NOT interchangeable.
// ---------------------------------------------------------------------------------------
task<void>
demo_bounded(Counter tally) {
    qb::io::cout() << "-- bounding one task: 300 ms of work, 100 ms of patience\n";

    // `coro_with_timeout(task&&, duration)` — a RELATIVE budget. It takes an rvalue and its
    // awaiter is neither copyable nor movable, so it is only ever written as the immediate
    // operand of `co_await`.
    auto t0 = std::chrono::steady_clock::now();
    try {
        (void) co_await coro_with_timeout(work("timeout-victim", 300ms, 1, tally), 100ms);
        qb::io::cout() << "    UNREACHABLE: 300 ms of work fitted in 100 ms\n";
    } catch (timeout_error const &) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        qb::io::cout() << "[coro_with_timeout] threw timeout_error after " << ms << " ms\n";
    }

    // ...and now wait past the inner task's own 300 ms. It prints. The timeout bounded OUR
    // wait; it did not touch the work. The header says so in as many words, and this is what
    // that sentence costs you if the work holds a lock or writes a row.
    co_await sleep(400ms);
    qb::io::cout() << "[coro_with_timeout] and the abandoned task FINISHED anyway — the timeout "
                   << "bounded the WAIT, not the WORK\n";

    // `with_deadline(task&&, steady_clock::time_point, token)` — an ABSOLUTE point in time,
    // and internally a `when_any` against a timer. That single implementation difference is
    // the whole behavioural difference: the losing branch is reclaimed like any other loser.
    // NOTE the time base: a `steady_clock::time_point`, not a `qb::duration` and not the
    // actor clock — `with_deadline` is a qb-io primitive with no engine underneath it.
    t0 = std::chrono::steady_clock::now();
    try {
        (void) co_await with_deadline(work("deadline-victim", 300ms, 1, tally), std::chrono::steady_clock::now() + 100ms);
        qb::io::cout() << "    UNREACHABLE: 300 ms of work fitted in the deadline\n";
    } catch (timeout_error const &) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        qb::io::cout() << "[with_deadline] threw timeout_error after " << ms << " ms\n";
    }
    co_await sleep(400ms);
    qb::io::cout() << "[with_deadline] and its branch was reclaimed: nothing finished behind our back. "
                      "THIS is the one to reach for when the work has side effects\n\n";
}

task<void>
run_all(Counter tally) {
    co_await demo_when_all(tally);
    co_await demo_when_any(tally);
    co_await demo_bounded(tally);
}

int
main() {
    qb::io::cout() << "=== combinators: when_all / when_any / race / timeout / deadline ===\n\n";

    // `init()` is a documented no-op kept for symmetry — `listener::current` is a
    // self-initialising thread_local. It is written here because every standalone qb-io
    // program in this corpus writes it, and a reader should not have to wonder whether the
    // one that omits it is the broken one.
    init();

    auto tally = std::make_shared<Tally>();

    // `run_sync(awaitable)` pumps the loop until the awaitable completes and then returns —
    // it is exact in both directions, unlike `run_for(d)`, which always burns its whole
    // budget and abandons anything still in flight. It must NOT be called from inside a
    // coroutine or an actor handler: it throws std::logic_error rather than re-entering the
    // drain it is already inside.
    run_sync(run_all(tally));

    // Ten branches begin; four are torn down mid-flight — the `when_any` loser, the two
    // `race`/vector losers, and the `with_deadline` branch. The `coro_with_timeout` victim is
    // the interesting one: it is abandoned and still finishes. The summary is a LITERAL and
    // it is gated on the measured numbers, so the runner's `@expect` check asserts the
    // BEHAVIOUR rather than merely that the program reached its last line.
    qb::io::cout() << "    counted: " << tally->started << " branches started, " << tally->finished << " finished\n";
    if (tally->started == 10 && tally->finished == 6)
        qb::io::cout() << "=== combinators complete: 6 of 10 branches finished, 4 were reclaimed ===\n";
    else
        qb::io::cout() << "=== combinators INCONCLUSIVE: the branch tally is not the expected 6 of 10 ===\n";
    return 0;
}
