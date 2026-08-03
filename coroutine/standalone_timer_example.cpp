/**
 * @file examples/coroutine/standalone_timer_example.cpp
 * @brief Standalone coroutine example using qb-io
 *
 * This example demonstrates using C++23 coroutines with qb-io
 * without the Actor system. It shows basic timer operations.
 *
 * "Standalone" means no actor framework -- not a hand-rolled compile. The build is the
 * ordinary one; this file is registered as the `standalone_timer_example` target in
 * examples/coroutine/CMakeLists.txt (DEPENDS qb-core qb-io).
 *
 * Build:
 *   cmake --preset dev && cmake --build --preset dev --target standalone_timer_example
 *
 * Run:
 *   ./build/presets/dev/bin/standalone_timer_example
 *
 * The previous note here gave `g++ -I qb/include -I /path/to/libev ... -lev`, which cannot
 * work: qb's include root is qb/src, the libev fork lives INSIDE it (reached as
 * <qb/vendor/qev/qev.h>, so it needs no -I of its own), the archive is libqev.a, and
 * linking qb-io also pulls OpenSSL/zlib -- flags a one-line example cannot enumerate.
 */

#include <chrono>
#include <iostream>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;

// Example 1: Simple delayed greeting
task<void>
delayed_greeting(const std::string &name, std::chrono::milliseconds delay) {
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

// Example 3: Concurrent operations
task<void>
concurrent_demo() {
    std::cout << "Starting concurrent operations...\n";

    // Launch multiple coroutines
    auto t1 = []() -> task<void> {
        co_await sleep(std::chrono::milliseconds(100));
        std::cout << "Task 1 completed\n";
    }();

    auto t2 = []() -> task<void> {
        co_await sleep(std::chrono::milliseconds(200));
        std::cout << "Task 2 completed\n";
    }();

    auto t3 = []() -> task<void> {
        co_await sleep(std::chrono::milliseconds(150));
        std::cout << "Task 3 completed\n";
    }();

    // Spawn all tasks
    coro_scheduler().spawn(std::move(t1));
    coro_scheduler().spawn(std::move(t2));
    coro_scheduler().spawn(std::move(t3));

    // Wait a bit for them to complete
    co_await sleep(std::chrono::milliseconds(300));

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

    // Example 1: Delayed greeting
    std::cout << "--- Example 1: Delayed Greeting ---\n";
    {
        auto t = delayed_greeting("World", std::chrono::milliseconds(100));
        coro_scheduler().spawn(std::move(t));
        run_for(std::chrono::milliseconds(200));
    }
    std::cout << "\n";

    // Example 2: Countdown
    std::cout << "--- Example 2: Countdown ---\n";
    {
        auto t = countdown(3);
        coro_scheduler().spawn(std::move(t));
        run_for(std::chrono::milliseconds(3000));
    }
    std::cout << "\n";

    // Example 3: Concurrent operations
    std::cout << "--- Example 3: Concurrent Operations ---\n";
    {
        auto t = concurrent_demo();
        coro_scheduler().spawn(std::move(t));
        run_for(std::chrono::milliseconds(500));
    }
    std::cout << "\n";

    // Example 4: Return values
    std::cout << "--- Example 4: Return Values ---\n";
    {
        auto t = use_computed_value();
        coro_scheduler().spawn(std::move(t));
        run_for(std::chrono::milliseconds(200));
    }
    std::cout << "\n";

    std::cout << "=== All examples completed! ===\n";

    return 0;
}
