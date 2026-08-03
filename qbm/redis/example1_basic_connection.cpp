/**
 * @file examples/qbm/redis/example1_basic_connection.cpp
 * @example qbm-redis: Basic Connection and String Operations
 *
 * @brief Connect to Redis and run common string operations using the modern **standalone qb-io**
 * coroutine flow (pure qb-io — no actor framework).
 *
 * @details
 * The qbm-redis client is coroutine-first: every command returns a `redis_awaiter` you `co_await`
 * to obtain a `qb::redis::Reply<T>` (`ok()` + `result()`; GET → `std::optional<std::string>`,
 * INCR/DEL → `long long`). The example logic lives in a `qb::io::async::task<void>` coroutine
 * spawned on the thread-local scheduler; `qb::io::async::run_until(running)` drives the event loop
 * until the coroutine signals completion (it flips `running` to false on any exit via a scope guard).
 *
 * QB/QBM Redis features demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::redis::tcp::client` + `co_await client.connect()`.
 * - `co_await client.set/get/incr/setex/del(...)` and `qb::redis::Reply<T>` (`ok()`/`result()`).
 */

#include <iostream>

#include <qbm/redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// Redis configuration - initializer-list form consumed by the client constructor.
#define REDIS_URI {"tcp://localhost:6379"}

// All Redis work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_basic_operations(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};

    // Connect - co_await yields a bool (true on success).
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!" << std::endl;

    // Basic SET operation.
    if (!(co_await redis.set("example:greeting", "Hello, Redis!")).ok()) {
        qb::io::cerr() << "SET operation failed" << std::endl;
        co_return;
    }
    qb::io::cout() << "SET operation successful" << std::endl;

    // Basic GET operation (result() is std::optional<std::string>).
    auto get_result = co_await redis.get("example:greeting");
    if (get_result.ok() && get_result.result().has_value()) {
        qb::io::cout() << "Retrieved value: " << *get_result.result() << std::endl;
    } else {
        qb::io::cerr() << "Key not found or GET operation failed" << std::endl;
        co_return;
    }

    // Using INCR for atomic counter operations (result() is long long).
    if (!(co_await redis.set("example:counter", "10")).ok()) {
        qb::io::cerr() << "SET counter failed" << std::endl;
        co_return;
    }
    auto incr_result = co_await redis.incr("example:counter");
    qb::io::cout() << "Counter value after INCR: " << incr_result.result() << std::endl;

    // Setting an expiration on a key.
    if (!(co_await redis.setex("example:temporary", 60, "This will expire in 60 seconds")).ok()) {
        qb::io::cerr() << "SETEX operation failed" << std::endl;
        co_return;
    }
    qb::io::cout() << "Set key with 60-second expiration" << std::endl;

    // Delete the keys created during the example (result() = number deleted).
    auto deleted = co_await redis.del("example:greeting", "example:counter", "example:temporary");
    qb::io::cout() << "Deleted " << deleted.result() << " keys" << std::endl;

    qb::io::cout() << "Basic Redis operations completed successfully!" << std::endl;
    co_return;
}

int
main() {
    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_basic_operations(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    return 0;
}
