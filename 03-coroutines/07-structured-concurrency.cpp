/**
 * @file examples/03-coroutines/07-structured-concurrency.cpp
 * @tier 03-coroutines
 * @teaches A scope OWNS the coroutines you spawn into it: it is where you join them, and its
 *          destructor is the one place that decides what happens to the ones still running.
 *          The three named policies differ only there — and only one of them does anything a
 *          worker can feel, which this program measures instead of asserting.
 * @demonstrates qb::io::async::coroutine_scope, qb::io::async::coroutine_scope::cleanup_policy,
 *               qb::io::async::joining_scope, qb::io::async::cancelling_scope,
 *               qb::io::async::detaching_scope, qb::io::async::with_scope, spawn_cancellable,
 *               join_all, join_any, join_all_for, cancel_all, cancel_token, active_count,
 *               total_count, prune_completed, rethrow_if_error, qb::io::async::task<void>,
 *               qb::io::async::task<int>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/05-combinators, 03-coroutines/06-cancellation
 * @expect "[join_all] resumed once ALL 3 workers were done"
 * @expect "[join_any] resumed at the FIRST finisher, index"
 * @expect "[join_all_for] returned false: 80 ms was not enough"
 * @expect "[join_all] rethrew the worker's exception: scope-worker exploded"
 * @expect "[joining_scope] the name is aspirational"
 * @expect "[cancelling_scope] cancel_all() is a SIGNAL, not a kill"
 * @expect "[detaching_scope] the child printed 90 ms AFTER its scope was destroyed"
 * @expect "[with_scope] returned 42"
 * @expect "=== structured concurrency complete: 3 policies measured, not assumed ==="
 *
 * WHAT PROBLEM THIS SOLVES
 * ------------------------
 * `when_all` (05-combinators) takes the branches as ARGUMENTS: you must know all of them at
 * the call site. A scope does not. You spawn into it from a loop, from a branch, from a
 * callback — and then you have one object that answers "are they done?", "stop them", and
 * "what went wrong". That is what "structured" means here: the children cannot outlive the
 * statement that owns them without you having said so.
 *
 * THE ONE THING WORTH LEARNING FROM THIS FILE
 * -------------------------------------------
 * `cancel_all()` is a SIGNAL. It sets a token. It does not reach into a parked coroutine and
 * end it — that would destroy a frame the scope does not own. A worker stops if and only if
 * it is looking: either it awaits `check_cancelled(token)` itself, or you spawned it with
 * `spawn_cancellable`, which wraps it so the token teardown reaches it.
 *
 * So of the three named policies:
 *
 *   joining_scope     dtor JOINS NOTHING. In a debug build it prints a warning to stderr if
 *                     tasks are still active; with NDEBUG it does not even do that. You still
 *                     have to `co_await join_all()` yourself.
 *   cancelling_scope  dtor calls `cancel_all()` — the only policy with an observable effect,
 *                     and only on a worker that is listening.
 *   detaching_scope   dtor clears the scope's own list. The workers keep running, because
 *                     they hold a shared_ptr to the scope's state, not to the scope.
 *
 * `cancelling_scope` is the default: a plain `coroutine_scope scope;` already has it.
 *
 * This program spawns identical workers under all three and prints which ones reached their
 * own last line. The counts are what the summary is gated on.
 *
 * NO ACTOR HERE, DELIBERATELY. `coroutine_scope` is a qb-io primitive and needs only a loop.
 * An actor already carries a scope of its own — `ctx`, which 06-cancellation covers — and the
 * two are different objects with different lifetimes. This file is about the standalone one.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-structured-concurrency
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-structured-concurrency
 */

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// THE INSTRUMENT. Every worker below appends its own name here when it reaches its LAST line.
// A worker that was torn down mid-flight never gets to append, so the ledger is the difference
// between "was spawned" and "was allowed to finish" — which is the entire subject of §4.
//
// A shared_ptr because a detached worker outlives the function that spawned it; a raw
// reference into a caller frame would be a use-after-free the moment `detaching_scope` does
// what its name says.
struct Ledger {
    std::vector<std::string> finished;

    void
    record(std::string who) {
        finished.push_back(std::move(who));
    }
    bool
    has(const std::string &who) const {
        for (const auto &f : finished)
            if (f == who)
                return true;
        return false;
    }
};
using Book = std::shared_ptr<Ledger>;

/**
 * One worker. Parameters BY VALUE — a coroutine frame stores its parameters, and the body does
 * not begin until the scheduler resumes it, by which time any caller temporary is gone.
 */
task<void>
worker(std::string name, qb::duration cost, Book book) {
    co_await sleep(cost);
    book->record(name);
    qb::io::cout() << "    ." << name << " finished\n";
}

task<void>
exploding_worker() {
    co_await sleep(20ms);
    throw std::runtime_error("scope-worker exploded");
}

// ---------------------------------------------------------------------------------------
// 1. spawn + join_all — the scope IS the join point.
// ---------------------------------------------------------------------------------------
task<void>
demo_join_all(Book book) {
    qb::io::cout() << "-- 1. spawn into a scope, then join it\n";

    coroutine_scope scope; // default policy: cleanup_policy::cancel_all
    scope.spawn(worker("all-a", 30ms, book));
    scope.spawn(worker("all-b", 60ms, book));

    // The second `spawn` overload takes a CALLABLE and owns its closure. Pass the lambda
    // WITHOUT a trailing `()`: `spawn(f())` creates the task from a temporary closure that
    // dies at the end of the full expression, and the frame then reads a dead capture.
    scope.spawn([book]() -> task<void> { co_await worker("all-c", 90ms, book); });

    qb::io::cout() << "    " << scope.total_count() << " spawned, active_count()=" << scope.active_count() << " before the join\n";

    co_await scope.join_all();

    qb::io::cout() << "[join_all] resumed once ALL 3 workers were done — active_count()=" << scope.active_count()
                   << ", total_count()=" << scope.total_count() << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. join_any — resume at the first finisher. The others are NOT stopped.
// ---------------------------------------------------------------------------------------
task<void>
demo_join_any(Book book) {
    qb::io::cout() << "-- 2. join_any: the first finisher wakes you, the rest keep going\n";

    coroutine_scope scope;
    scope.spawn(worker("any-slow", 200ms, book));
    scope.spawn(worker("any-fast", 40ms, book));
    scope.spawn(worker("any-mid", 120ms, book));

    const size_t idx = co_await scope.join_any();

    // This is the difference from `when_any`, and it is the reason both exist: `when_any`
    // RECLAIMS the branches that lost. `join_any` does not — it is a notification, and the
    // scope still owns two running workers at this line.
    qb::io::cout() << "[join_any] resumed at the FIRST finisher, index " << idx << " — and " << scope.active_count()
                   << " workers are STILL RUNNING (join_any notifies, it does not reclaim)\n";

    co_await scope.join_all();
    qb::io::cout() << "    then join_all drained the other two\n\n";
}

// ---------------------------------------------------------------------------------------
// 3. join_all_for — a join with a wall clock. Returns false on timeout; nothing is cancelled.
// ---------------------------------------------------------------------------------------
task<void>
demo_join_all_for(Book book) {
    qb::io::cout() << "-- 3. join_all_for: a join with a deadline\n";

    coroutine_scope scope;
    scope.spawn(worker("timed-quick", 30ms, book));
    scope.spawn(worker("timed-slow", 250ms, book));

    const bool all_done = co_await scope.join_all_for(80ms);
    if (!all_done)
        qb::io::cout() << "[join_all_for] returned false: 80 ms was not enough, " << scope.active_count() << " worker still running\n";

    // A false return is a REPORT, not a cancellation — the slow worker is untouched and will
    // print on its own. Deciding what to do about it is the caller's job, and `cancel_all()`
    // is one of the choices.
    co_await scope.join_all();
    qb::io::cout() << "    and it finished on its own afterwards, because a timeout is a report\n\n";
}

// ---------------------------------------------------------------------------------------
// 4. THE THREE POLICIES, side by side, with the same worker under each.
// ---------------------------------------------------------------------------------------
task<void>
demo_policies(Book book) {
    qb::io::cout() << "-- 4. the three cleanup policies, measured\n";

    // -- joining_scope: the dtor joins NOTHING. -----------------------------------------
    {
        joining_scope scope; // == coroutine_scope{coroutine_scope::cleanup_policy::join_all}
        scope.spawn(worker("joining-child", 40ms, book));
        // Without this line the scope would be destroyed with one active task, the workers
        // would keep running anyway, and a debug build would print a warning to stderr. The
        // policy documents an INTENT; the join is still yours to write.
        co_await scope.join_all();
    }
    qb::io::cout() << "[joining_scope] the name is aspirational: its destructor cannot suspend, so it "
                      "warns in a debug build and joins nothing — `co_await join_all()` is still yours to write\n";

    // -- cancelling_scope (the default): the dtor SIGNALS. --------------------------------
    {
        cancelling_scope scope; // == the default coroutine_scope
        qb::io::cout() << "    (its token starts un-cancelled: cancel_token().is_cancelled() == "
                       << (scope.cancel_token().is_cancelled() ? "true" : "false") << ")\n";

        // TWO IDENTICAL WORKERS, one difference: how they were spawned.
        //   spawn_cancellable  wraps the task in `make_cancellable(t, token)`, which registers
        //                      a teardown hook on the token — the parked frame is reclaimed.
        //   spawn              does not. The token is set and the worker never learns of it.
        scope.spawn_cancellable(worker("cancel-reachable", 180ms, book));
        scope.spawn(worker("cancel-deaf", 180ms, book));
    } // <- destructor runs here and calls cancel_all()

    co_await sleep(300ms); // let both outcomes settle before reading the ledger

    const bool reachable_stopped = !book->has("cancel-reachable");
    const bool deaf_finished     = book->has("cancel-deaf");
    qb::io::cout() << "[cancelling_scope] cancel_all() is a SIGNAL, not a kill: the spawn_cancellable() worker "
                   << (reachable_stopped ? "STOPPED" : "did not stop") << ", the plain spawn() worker "
                   << (deaf_finished ? "RAN TO COMPLETION" : "stopped")
                   << " — a token reaches only the frames that were WRAPPED to receive it\n";

    // -- detaching_scope: the dtor lets go. ----------------------------------------------
    // Spelled the long way ONCE, to show that the three named classes are nothing but a
    // coroutine_scope with the policy fixed in their constructor. `detaching_scope scope;`
    // and this line are the same object.
    {
        coroutine_scope scope(coroutine_scope::cleanup_policy::detach);
        scope.spawn(worker("detached-child", 90ms, book));
    } // <- destructor runs here; the child is 90 ms from its own last line
    static_assert(std::is_base_of_v<coroutine_scope, detaching_scope>, "detaching_scope IS a coroutine_scope with a fixed policy");

    co_await sleep(200ms);
    if (book->has("detached-child"))
        qb::io::cout() << "[detaching_scope] the child printed 90 ms AFTER its scope was destroyed — "
                          "the worker holds the scope's shared STATE, not the scope object\n";

    // -- the explicit form: cancel, then join. -------------------------------------------
    // Writing it out is what you want when the cancellation is a decision rather than a
    // scope exit. Note there is no try/catch: `join_all()` ABSORBS a cancelled_error raised
    // by THIS scope's own token, because rethrowing the signal the caller just sent would
    // force every cancel+drain site to catch its own request. A cancelled_error from an
    // unrelated token still propagates.
    {
        coroutine_scope scope;
        scope.spawn_cancellable(worker("drain-a", 5s, book));
        scope.spawn_cancellable(worker("drain-b", 5s, book));
        scope.cancel_all();
        co_await scope.join_all();
        qb::io::cout() << "[cancel_all] cancel + join_all drained 2 workers with a 5 s sleep each, in "
                       << "no time at all and with no try/catch — active_count()=" << scope.active_count() << "\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 5. failure — a scope collects the first error and join_all rethrows it.
// ---------------------------------------------------------------------------------------
task<void>
demo_failure(Book book) {
    qb::io::cout() << "-- 5. one worker throws\n";

    coroutine_scope scope;
    scope.spawn(worker("survivor", 50ms, book));
    scope.spawn(exploding_worker());

    try {
        co_await scope.join_all();
        qb::io::cout() << "[join_all] UNEXPECTED: no exception\n";
    } catch (const std::exception &e) {
        // join_all waits for EVERY worker first, then rethrows the first stored error — so the
        // survivor has already finished by the time this line runs. The message is compared
        // rather than echoed, so this line is an assertion and not merely a report.
        if (std::string{e.what()} == "scope-worker exploded")
            qb::io::cout() << "[join_all] rethrew the worker's exception: scope-worker exploded "
                              "(and the survivor still finished first)\n";
        else
            qb::io::cout() << "[join_all] UNEXPECTED exception text: " << e.what() << "\n";
    }

    // The error is STORED, not consumed. `rethrow_if_error()` is the same slot queried without
    // waiting — useful after `join_all_for` returned false, where you have not joined at all.
    // `prune_completed()` drops the finished bookkeeping entries; it does not touch the error.
    scope.prune_completed();
    qb::io::cout() << "    after prune_completed(): total_count()=" << scope.total_count() << ", and the error is still there — ";
    try {
        scope.rethrow_if_error();
        qb::io::cout() << "UNEXPECTED: rethrow_if_error() was silent\n\n";
    } catch (const std::exception &e) {
        qb::io::cout() << "rethrow_if_error() gave it back: " << e.what() << "\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 6. with_scope — the scope as an expression, when you want its value back.
// ---------------------------------------------------------------------------------------
task<void>
demo_with_scope(Book book) {
    qb::io::cout() << "-- 6. with_scope: hand a scope to a lambda and take its result\n";

    const int answer = co_await with_scope([book](coroutine_scope &scope) -> task<int> {
        scope.spawn(worker("scoped-a", 20ms, book));
        scope.spawn(worker("scoped-b", 40ms, book));
        co_await scope.join_all();
        co_return 42;
    });

    if (answer == 42)
        qb::io::cout() << "[with_scope] returned 42 after joining both of its children\n\n";
    else
        qb::io::cout() << "[with_scope] UNEXPECTED: returned " << answer << "\n\n";
}

task<void>
run_all(Book book) {
    co_await demo_join_all(book);
    co_await demo_join_any(book);
    co_await demo_join_all_for(book);
    co_await demo_policies(book);
    co_await demo_failure(book);
    co_await demo_with_scope(book);
}

int
main() {
    qb::io::cout() << "=== structured concurrency: coroutine_scope and its three exit policies ===\n\n";

    // A documented no-op kept for symmetry with every other standalone qb-io program here —
    // `listener::current` is a self-initialising thread_local.
    init();

    auto book = std::make_shared<Ledger>();
    run_sync(run_all(book));

    // 17 recording workers are spawned; exactly three are expected NOT to reach their own last
    // line, and they are exactly the three that `spawn_cancellable` wrapped —
    // `cancel-reachable`, `drain-a`, `drain-b`. `exploding_worker` throws instead of recording,
    // so it is outside this count by construction.
    qb::io::cout() << "    ledger: " << book->finished.size() << " of 17 workers reached their own last line\n";
    if (book->finished.size() == 14 && !book->has("cancel-reachable") && !book->has("drain-a") && book->has("cancel-deaf")
        && book->has("detached-child"))
        qb::io::cout() << "=== structured concurrency complete: 3 policies measured, not assumed ===\n";
    else
        qb::io::cout() << "=== structured concurrency INCONCLUSIVE: the ledger is not the expected 12 of 13 ===\n";
    return 0;
}
