/**
 * @file examples/03-coroutines/01-first-coroutine.cpp
 * @tier 03-coroutines
 * @teaches A coroutine with no actor and no engine underneath it: what task<T> is, what drives
 *          it, how several of them run at once — and the by-value parameter rule, which is the
 *          one mistake here that produces live undefined behaviour rather than a compile error.
 * @demonstrates qb::io::async::task<void>, qb::io::async::task<int>, qb::io::async::run_sync,
 *               qb::io::async::sleep, qb::io::async::when_all
 * @prerequisites 02-io/01-event-loop
 * @expect "=== QB Coroutine Standalone Example ==="
 * @expect "All concurrent operations done!"
 * @expect "=== All examples completed! ==="
 *
 * @brief Standalone coroutine example using qb-io
 *
 * This example demonstrates using C++20 coroutines with qb-io
 * without the Actor system. It shows basic timer operations.
 *
 * Coroutines are a C++20 feature and C++20 is this project's default standard; the previous
 * note here said "C++23 coroutines", which no part of this file needs.
 *
 * "Standalone" means no actor framework -- not a hand-rolled compile. The build is the
 * ordinary one, and neither the target nor the binary name is written down anywhere: both are
 * DERIVED from this file's path by `qb_example` in examples/03-coroutines/CMakeLists.txt.
 *
 * Build:
 *   cmake --preset dev && cmake --build --preset dev --target qb-example-coroutines-first-coroutine
 *
 * Run:
 *   ./build/presets/dev/examples/03-coroutines/qb-example-coroutines-first-coroutine
 *
 * The previous note here gave `g++ -I qb/include -I /path/to/libev ... -lev`, which cannot
 * work: qb's include root is qb/src, the libev fork lives INSIDE it (reached as
 * <qb/ev/qev.h>, so it needs no -I of its own), the archive is libqev.a, and
 * linking qb-io also pulls OpenSSL/zlib -- flags a one-line example cannot enumerate.
 */

#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;

// ============================================================================================
// RULE 1 -- A COROUTINE PARAMETER IS STORED IN THE FRAME, SO TAKE IT BY VALUE.
//
// `name` is a `std::string`, NOT a `const std::string &`. A coroutine's frame stores its
// parameters, and a reference parameter stores the REFERENCE, not the object. `initial_suspend`
// is `suspend_always`, so the body does not start at the call -- it starts when the scheduler
// resumes the frame, by which time the caller's temporary
// (`delayed_greeting("World", 100ms)` materialises one) is long destroyed.
//
// This is not theoretical: with `const std::string &` here the release build printed
// "Hello, " followed by binary garbage -- three runs, three different strings -- and exited 0,
// while the `sanitize` preset reported `stack-use-after-scope` and SIGABRTed on Example 1,
// so Examples 2-4 below never ran under sanitizers at all.
//
// Take EVERY coroutine parameter by value (or by a type that owns its bytes). The one copy is
// the price of the frame outliving the call site.
// ============================================================================================

// Example 1: Simple delayed greeting
task<void>
delayed_greeting(std::string name, std::chrono::milliseconds delay) {
    std::cout << "Waiting for " << delay.count() << "ms...\n";

    co_await sleep(delay);

    std::cout << "Hello, " << name << "!\n";
}

// Example 2: Sequential delays
task<void>
countdown(int start) {
    for (int i = start; i > 0; --i) {
        std::cout << i << "...\n";
        co_await sleep(std::chrono::milliseconds(500));
    }
    std::cout << "Launch!\n";
}

// ============================================================================================
// RULE 2 -- DO NOT WRITE `[]() -> task<void> { ... }()`.
//
// The immediately-invoked coroutine lambda is the shape
// qb/src/qb/io/async/coroutine/scheduler.h labels BROKEN, and qb ships a dedicated lint for it
// (qb/scripts/check-spawn-dangling-closure.py): the closure object is a temporary that dies at
// the end of the full-expression, while the frame it produced keeps referring to its captures.
// It only "works" with an EMPTY capture list -- one added capture is ASan-invisible corruption.
//
// A named coroutine function takes its state as by-value parameters (Rule 1) and has no
// closure to dangle.
// ============================================================================================

// A named unit of work, used below by `when_all`.
task<void>
labelled_delay(std::string label, std::chrono::milliseconds delay) {
    co_await sleep(delay);
    std::cout << label << " completed\n";
}

// Example 3: Concurrent operations
task<void>
concurrent_demo() {
    std::cout << "Starting concurrent operations...\n";

    // `when_all` resumes when the LAST branch finishes, so the line below is printed on
    // completion. The previous version spawned three detached tasks and then slept 300ms,
    // printing "All concurrent operations done!" on a guess: 300ms happened to exceed the
    // longest branch, so the claim was true by arithmetic rather than by observation, and a
    // slower machine would have printed it early.
    co_await when_all(labelled_delay("Task 1", std::chrono::milliseconds(100)), labelled_delay("Task 2", std::chrono::milliseconds(200)),
                      labelled_delay("Task 3", std::chrono::milliseconds(150)));

    std::cout << "All concurrent operations done!\n";
}

// Example 4: Task with return value
task<int>
compute_value(int base) {
    co_await sleep(std::chrono::milliseconds(100));
    co_return base * 2;
}

task<void>
use_computed_value() {
    std::cout << "Computing value...\n";

    int result = co_await compute_value(21);

    std::cout << "Result: " << result << "\n";
}

int
main() {
    std::cout << "=== QB Coroutine Standalone Example ===\n\n";

    // Initialize async system
    qb::io::async::init();

    // ========================================================================================
    // RULE 3 -- `run_sync(awaitable)` RETURNS WHEN THE WORK IS DONE; `run_for(d)` RETURNS
    // WHEN `d` IS UP.
    //
    // Each block below used `run_for(<guessed duration>)`, which always burns its whole budget
    // (see qb/src/qb/io/async/coroutine/utils.h) -- 3.9s of budget for ~2.0s of work here,
    // roughly half of it idle spinning -- and is simultaneously a CORRECTNESS guess: pick the
    // duration too small on a loaded machine and the coroutine is abandoned mid-flight with no
    // diagnostic and exit code 0.
    //
    // `run_sync` is exact in both directions: it pumps the loop until the awaitable completes,
    // then returns its value (or rethrows its exception).
    // ========================================================================================

    // Example 1: Delayed greeting
    std::cout << "--- Example 1: Delayed Greeting ---\n";
    run_sync(delayed_greeting("World", std::chrono::milliseconds(100)));
    std::cout << "\n";

    // Example 2: Countdown
    std::cout << "--- Example 2: Countdown ---\n";
    run_sync(countdown(3));
    std::cout << "\n";

    // Example 3: Concurrent operations
    std::cout << "--- Example 3: Concurrent Operations ---\n";
    run_sync(concurrent_demo());
    std::cout << "\n";

    // Example 4: Return values
    std::cout << "--- Example 4: Return Values ---\n";
    run_sync(use_computed_value());
    std::cout << "\n";

    std::cout << "=== All examples completed! ===\n";

    return 0;
}
