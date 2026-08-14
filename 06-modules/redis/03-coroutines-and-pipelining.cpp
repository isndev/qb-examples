/**
 * @file examples/06-modules/redis/03-coroutines-and-pipelining.cpp
 * @tier 06-modules
 * @teaches Awaiting Redis, and what pipelining is: three commands issued inside one when_all leave
 *          together and come back together, where three sequential co_awaits pay three round trips.
 * @demonstrates qb::redis::tcp::client, qb::redis::Reply<T>, qb::io::async::when_all,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/01-connect
 * @expect "=== Example 1: Simple GET/SET ==="
 * @expect "=== Example 3: Multiple Keys ==="
 * @brief Example demonstrating Redis coroutine API with pure qb-io
 *
 * This example shows how to use the coroutine-based Redis API for clean, linear async
 * code without callback hell.  Commands are issued directly on `qb::redis::tcp::client`
 * and `co_await`ed — no separate coro-wrapper class is needed.
 *
 * Standalone qb-io driver: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 *
 * QB features demonstrated:
 * - Linear async code with `co_await` (no callbacks).
 * - `qb::io::async::when_all(...)` to run independent commands in parallel /
 *   pipelined (see example_multiple_keys()).
 *
 * PURE QB-IO - NO ACTORS!
 */

#include <iostream>
#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

/**
 * @brief Example 1: Simple GET/SET operations
 */
qb::io::async::task<void>
example_simple_get_set() {
    std::cout << "\n=== Example 1: Simple GET/SET ===" << std::endl;

    // Create Redis client and connect using co_await (yields bool).
    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }

    // Set a value - linear code!
    auto set_reply = co_await redis.set("user:1:name", "Alice");
    if (set_reply.ok()) {
        std::cout << "SET user:1:name = Alice" << std::endl;
    } else {
        std::cerr << "SET failed" << std::endl;
    }

    // Get the value back.
    // get() returns Reply<optional<string>>; result() is optional<string>&.
    auto get_reply = co_await redis.get("user:1:name");
    if (get_reply.ok() && get_reply.result().has_value()) {
        std::cout << "GET user:1:name = " << *get_reply.result() << std::endl;
    } else {
        std::cerr << "GET failed or key not found" << std::endl;
    }
}

/**
 * @brief Example 2: Sequential operations with error handling
 */
qb::io::async::task<void>
example_sequential_operations() {
    std::cout << "\n=== Example 2: Sequential Operations ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }

    // Setup test data - hset returns Reply<long long> (new fields added).
    (void) (co_await redis.hset("user:123", "name", "Bob"));
    (void) (co_await redis.hset("user:123", "email", "bob@example.com"));
    (void) (co_await redis.hset("user:123", "age", "30"));

    std::cout << "User data created" << std::endl;

    // Fetch user profile - multiple operations in sequence.
    // hget returns Reply<optional<string>>; result() is optional<string>&.
    auto name  = co_await redis.hget("user:123", "name");
    auto email = co_await redis.hget("user:123", "email");
    auto age   = co_await redis.hget("user:123", "age");

    if (name.ok() && email.ok() && age.ok()) {
        std::cout << "User Profile:" << std::endl;
        std::cout << "  Name:  " << name.result().value_or("N/A") << std::endl;
        std::cout << "  Email: " << email.result().value_or("N/A") << std::endl;
        std::cout << "  Age:   " << age.result().value_or("N/A") << std::endl;
    }
}

/**
 * @brief Example 3: Multiple keys operations — parallel fetch with when_all
 */
qb::io::async::task<void>
example_multiple_keys() {
    std::cout << "\n=== Example 3: Multiple Keys ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }

    // Set multiple keys
    (void) (co_await redis.set("key1", "value1"));
    (void) (co_await redis.set("key2", "value2"));
    (void) (co_await redis.set("key3", "value3"));

    std::cout << "Created 3 keys" << std::endl;

    // Fetch all three keys in PARALLEL with when_all instead of one-by-one.
    //
    // when_all takes real qb::io::async::task<T> objects, not raw command
    // awaiters, so we wrap each GET in a tiny local coroutine. The `fetch`
    // lambda outlives the when_all call, so capturing `&redis` by reference is
    // safe. get() yields Reply<optional<string>>.
    //
    // Because every command runs on the same client, the three GETs are
    // pipelined: they go out together and the responses are awaited as one
    // round-trip group rather than three sequential request/response cycles.
    auto fetch = [&redis](std::string key) -> qb::io::async::task<qb::redis::Reply<std::optional<std::string>>> {
        co_return co_await redis.get(key);
    };

    auto [r1, r2, r3] = co_await qb::io::async::when_all(fetch("key1"), fetch("key2"), fetch("key3"));

    int success = 0;
    if (r1.ok() && r1.result().has_value())
        ++success;
    if (r2.ok() && r2.result().has_value())
        ++success;
    if (r3.ok() && r3.result().has_value())
        ++success;

    std::cout << "Fetched " << success << "/3 keys successfully" << std::endl;

    // Cleanup - del() returns Reply<long long> (keys deleted).
    (void) (co_await redis.del("key1", "key2", "key3"));
    std::cout << "Cleaned up test keys" << std::endl;
}

/**
 * @brief Example 4: Error handling patterns
 */
qb::io::async::task<void>
example_error_handling() {
    std::cout << "\n=== Example 4: Error Handling ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }

    // Try to get a non-existent key.
    auto reply = co_await redis.get("non_existent_key");

    if (reply.ok()) {
        if (reply.result().has_value()) {
            std::cout << "Value: " << *reply.result() << std::endl;
        } else {
            std::cout << "Key not found (expected)" << std::endl;
        }
    } else {
        std::cerr << "Redis error: " << reply.error() << std::endl;
    }
}

/**
 * @brief Main coroutine that runs all examples.
 *
 * `running` is flipped to false on ANY exit path (scope guard) so the
 * run_until() loop in main() stops once this coroutine is done.
 */
qb::io::async::task<void>
run_all_examples(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    std::cout << "QB Redis Coroutine Examples (Pure qb-io)" << std::endl;
    std::cout << "Make sure Redis is running on localhost:6379" << std::endl;

    co_await example_simple_get_set();
    co_await example_sequential_operations();
    co_await example_multiple_keys();
    co_await example_error_handling();

    std::cout << "\n=== All Examples Complete ===" << std::endl;
    co_return;
}

int
main(int /*argc*/, char * /*argv*/[]) {
    // Initialize qb-io async system
    qb::io::async::init();

    // Spawn the main coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_all_examples(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    std::cout << "\nExamples completed successfully!" << std::endl;
    return 0;
}
