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
qb::io::async::task<bool>
example_simple_get_set() {
    std::cout << "\n=== Example 1: Simple GET/SET ===" << std::endl;

    // Create Redis client and connect using co_await (yields bool).
    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return false;
    }

    // Set a value - linear code!
    auto set_reply = co_await redis.set("user:1:name", "Alice");
    if (set_reply.ok()) {
        std::cout << "SET user:1:name = Alice" << std::endl;
    } else {
        std::cerr << "SET failed" << std::endl;
        co_return false;
    }

    // Get the value back.
    // get() returns Reply<optional<string>>; result() is optional<string>&.
    auto get_reply = co_await redis.get("user:1:name");
    if (get_reply.ok() && get_reply.result() == std::optional<std::string>{"Alice"}) {
        std::cout << "GET user:1:name = " << *get_reply.result() << std::endl;
    } else {
        std::cerr << "GET failed or key not found" << std::endl;
        co_return false;
    }

    // Example 3 already deleted its own keys and these two did not, which is the corpus rule
    // ("every key a program writes is deleted on the way out") broken in the one file nobody
    // re-read after writing it. Measured: `user:1:name` and `user:123` survived every run.
    auto removed = co_await redis.del("user:1:name");
    co_return removed.ok() && removed.result() == 1;
}

/**
 * @brief Example 2: Sequential operations with error handling
 */
qb::io::async::task<bool>
example_sequential_operations() {
    std::cout << "\n=== Example 2: Sequential Operations ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return false;
    }

    // Setup test data - hset returns Reply<long long> (new fields added).
    auto set_name  = co_await redis.hset("user:123", "name", "Bob");
    auto set_email = co_await redis.hset("user:123", "email", "bob@example.com");
    auto set_age   = co_await redis.hset("user:123", "age", "30");
    if (!set_name.ok() || !set_email.ok() || !set_age.ok())
        co_return false;

    std::cout << "User data created" << std::endl;

    // Fetch user profile - multiple operations in sequence.
    // hget returns Reply<optional<string>>; result() is optional<string>&.
    auto name  = co_await redis.hget("user:123", "name");
    auto email = co_await redis.hget("user:123", "email");
    auto age   = co_await redis.hget("user:123", "age");

    const bool values_ok = name.ok() && name.result() == std::optional<std::string>{"Bob"} && email.ok()
                           && email.result() == std::optional<std::string>{"bob@example.com"} && age.ok()
                           && age.result() == std::optional<std::string>{"30"};
    if (values_ok) {
        std::cout << "User Profile:" << std::endl;
        std::cout << "  Name:  " << name.result().value_or("N/A") << std::endl;
        std::cout << "  Email: " << email.result().value_or("N/A") << std::endl;
        std::cout << "  Age:   " << age.result().value_or("N/A") << std::endl;
    } else {
        co_return false;
    }

    // As in example 1: this hash used to be left on the server after every run.
    auto removed = co_await redis.del("user:123");
    co_return removed.ok() && removed.result() == 1;
}

/**
 * @brief Example 3: Multiple keys operations — parallel fetch with when_all
 */
qb::io::async::task<bool>
example_multiple_keys() {
    std::cout << "\n=== Example 3: Multiple Keys ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return false;
    }

    // Set multiple keys
    auto s1 = co_await redis.set("key1", "value1");
    auto s2 = co_await redis.set("key2", "value2");
    auto s3 = co_await redis.set("key3", "value3");
    if (!s1.ok() || !s2.ok() || !s3.ok())
        co_return false;

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
    if (r1.ok() && r1.result() == std::optional<std::string>{"value1"})
        ++success;
    if (r2.ok() && r2.result() == std::optional<std::string>{"value2"})
        ++success;
    if (r3.ok() && r3.result() == std::optional<std::string>{"value3"})
        ++success;

    std::cout << "Fetched " << success << "/3 keys successfully" << std::endl;

    // Cleanup - del() returns Reply<long long> (keys deleted).
    auto removed = co_await redis.del("key1", "key2", "key3");
    std::cout << "Cleaned up test keys" << std::endl;
    co_return success == 3 && removed.ok() && removed.result() == 3;
}

/**
 * @brief Example 4: Error handling patterns
 */
qb::io::async::task<bool>
example_error_handling() {
    std::cout << "\n=== Example 4: Error Handling ===" << std::endl;

    qb::redis::tcp::client redis{{"tcp://localhost:6379"}};
    if (!co_await redis.connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return false;
    }

    // Try to get a non-existent key.
    auto reply = co_await redis.get("non_existent_key");

    if (reply.ok()) {
        if (reply.result().has_value()) {
            std::cout << "Value: " << *reply.result() << std::endl;
        } else {
            std::cout << "Key not found (expected)" << std::endl;
            co_return true;
        }
    } else {
        std::cerr << "Redis error: " << reply.error() << std::endl;
    }
    co_return false;
}

/**
 * @brief Main coroutine that runs all examples.
 *
 * `running` is flipped to false on ANY exit path (scope guard) so the
 * run_until() loop in main() stops once this coroutine is done.
 */
qb::io::async::task<void>
run_all_examples(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    std::cout << "QB Redis Coroutine Examples (Pure qb-io)" << std::endl;
    std::cout << "Make sure Redis is running on localhost:6379" << std::endl;

    ok = co_await example_simple_get_set();
    ok = (co_await example_sequential_operations()) && ok;
    ok = (co_await example_multiple_keys()) && ok;
    ok = (co_await example_error_handling()) && ok;

    if (ok)
        std::cout << "\n=== All Examples Complete ===" << std::endl;
    co_return;
}

int
main(int /*argc*/, char * /*argv*/[]) {
    // Initialize qb-io async system
    qb::io::async::init();

    // Spawn the main coroutine and drive the event loop until it completes.
    bool running = true;
    bool ok      = false;
    auto task    = run_all_examples(running, ok);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    if (ok)
        std::cout << "\nExamples completed successfully!" << std::endl;
    return ok ? 0 : 1;
}
