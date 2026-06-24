/**
 * @file examples/qbm/redis/example2_hash_operations.cpp
 * @example qbm-redis: Hash Data Structure Operations
 *
 * @brief Demonstrates how to use Redis Hash commands via the `qbm-redis` client to store,
 * retrieve, and manage structured data (e.g. a user profile), using the modern
 * **standalone qb-io** coroutine flow (pure qb-io — no actor framework).
 *
 * @details
 * The qbm-redis client is coroutine-first: every command returns a `redis_awaiter` that
 * you `co_await` to obtain a `qb::redis::Reply<T>`. The reply exposes `ok()` (command
 * succeeded) and `result()` (the typed value). All Redis work lives in a free
 * `qb::io::async::task<void>` coroutine; `qb::io::async::run_until(running)` drives the
 * event loop until the coroutine signals completion (it flips `running` to false on any
 * exit via a scope guard).
 *
 * The program performs the following sequence of operations:
 * 1.  `co_await redis.connect()` — establish the connection (yields `bool`).
 * 2.  Hash field commands, each `co_await`ed:
 *     - `hset(key, field, value)` -> `Reply<long long>`                (fields added)
 *     - `hget(key, field)`        -> `Reply<optional<string>>`         (`result().has_value()`)
 *     - `hexists(key, field)`     -> `Reply<bool>`                     (`result()`)
 *     - `hincrby(key, field, n)`  -> `Reply<long long>`                (`result()`)
 *     - `hgetall(key)`            -> `Reply<unordered_map<str,str>>`   (iterable pairs)
 *     - `hkeys(key)`              -> `Reply<vector<string>>`           (field names)
 *     - `hvals(key)`              -> `Reply<vector<string>>`           (field values)
 *     - `hlen(key)`               -> `Reply<long long>`                (field count)
 *     - `del(key)`                -> `Reply<long long>`                (keys deleted)
 *
 * QB/QBM Redis Features Demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::redis::tcp::client` + `co_await client.connect()`.
 * - `co_await client.<command>()` and `qb::redis::Reply<T>`: `ok()` and `result()`.
 * - `qb::io::async::when_all(...)` — fetch independent fields in parallel / pipelined.
 * - `qb::io::cout()` — thread-safe console output.
 */

#include <iostream>

#include <redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// All Redis work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_hash_operations(bool &running) {
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

    // Generate a unique key for this example to avoid collisions
    std::string user_key = "example:user:profile:1001";

    // Clean up any existing data
    (void) (co_await redis.del(user_key));

    // -------- Setting multiple hash fields at once --------
    // Create a user profile using individual HSET calls.
    // hset returns Reply<long long> = number of NEW fields added.
    long long hset_count = 0;
    hset_count += (co_await redis.hset(user_key, "username", "johndoe")).result();
    hset_count += (co_await redis.hset(user_key, "first_name", "John")).result();
    hset_count += (co_await redis.hset(user_key, "last_name", "Doe")).result();
    hset_count += (co_await redis.hset(user_key, "email", "john.doe@example.com")).result();
    hset_count += (co_await redis.hset(user_key, "active", "1")).result();
    hset_count += (co_await redis.hset(user_key, "age", "30")).result();
    hset_count += (co_await redis.hset(user_key, "registration_date", "2023-01-15")).result();

    qb::io::cout() << "Added " << hset_count << " new fields to hash" << std::endl;

    // -------- Getting a single field from the hash --------
    // hget returns Reply<optional<string>>; use result().has_value() / *result()
    auto username_r = co_await redis.hget(user_key, "username");
    if (username_r.ok() && username_r.result().has_value()) {
        qb::io::cout() << "Username: " << *username_r.result() << std::endl;
    } else {
        qb::io::cout() << "Username field not found" << std::endl;
    }

    // -------- Getting multiple fields at once --------
    // These three HGETs are independent, so fetch them in PARALLEL with
    // when_all instead of one-by-one. when_all takes real
    // qb::io::async::task<T> objects, not raw command awaiters, so we wrap each
    // HGET in a tiny local coroutine. The `fetch_field` lambda outlives the
    // when_all call, so capturing `&redis` / `&user_key` by reference is safe.
    // Because every command runs on the same client, the three reads are
    // pipelined into a single round-trip group. hget yields Reply<optional<string>>.
    auto fetch_field = [&redis, &user_key](std::string field) -> qb::io::async::task<qb::redis::Reply<std::optional<std::string>>> {
        co_return co_await redis.hget(user_key, field);
    };

    auto [first_name_r, last_name_r, nonexistent_r] =
        co_await qb::io::async::when_all(fetch_field("first_name"), fetch_field("last_name"), fetch_field("nonexistent_field"));

    qb::io::cout() << "HMGET results:" << std::endl;
    if (first_name_r.ok() && first_name_r.result().has_value()) {
        qb::io::cout() << "  first_name: " << *first_name_r.result() << std::endl;
    } else {
        qb::io::cout() << "  first_name: (nil)" << std::endl;
    }

    if (last_name_r.ok() && last_name_r.result().has_value()) {
        qb::io::cout() << "  last_name: " << *last_name_r.result() << std::endl;
    } else {
        qb::io::cout() << "  last_name: (nil)" << std::endl;
    }

    if (nonexistent_r.ok() && nonexistent_r.result().has_value()) {
        qb::io::cout() << "  nonexistent_field: " << *nonexistent_r.result() << std::endl;
    } else {
        qb::io::cout() << "  nonexistent_field: (nil)" << std::endl;
    }

    // -------- Checking if a field exists --------
    // hexists returns Reply<bool>
    auto exists_r = co_await redis.hexists(user_key, "email");
    qb::io::cout() << "Email field exists: " << (exists_r.ok() && exists_r.result() ? "true" : "false") << std::endl;

    // -------- Incrementing numeric fields --------
    // hincrby returns Reply<long long>
    auto new_age_r = co_await redis.hincrby(user_key, "age", 1);
    qb::io::cout() << "Age after increment: " << new_age_r.result() << std::endl;

    // -------- Getting all fields and values --------
    // hgetall returns Reply<qb::unordered_map<string, string>>
    auto all_data_r = co_await redis.hgetall(user_key);
    qb::io::cout() << "All hash fields for " << user_key << ":" << std::endl;
    if (all_data_r.ok()) {
        for (const auto &[field, value] : all_data_r.result()) {
            qb::io::cout() << "  " << field << ": " << value << std::endl;
        }
    }

    // -------- Getting only field names or values --------
    // hkeys / hvals return Reply<vector<string>>
    auto fields_r = co_await redis.hkeys(user_key);
    if (fields_r.ok()) {
        const auto &fields_list = fields_r.result();
        qb::io::cout() << "Hash fields: ";
        for (size_t i = 0; i < fields_list.size(); i++) {
            qb::io::cout() << fields_list[i] << (i < fields_list.size() - 1 ? ", " : "");
        }
        qb::io::cout() << std::endl;
    }

    auto values_r = co_await redis.hvals(user_key);
    if (values_r.ok()) {
        const auto &values_list = values_r.result();
        qb::io::cout() << "Hash values: ";
        for (size_t i = 0; i < values_list.size(); i++) {
            qb::io::cout() << values_list[i] << (i < values_list.size() - 1 ? ", " : "");
        }
        qb::io::cout() << std::endl;
    }

    // -------- Getting the number of fields --------
    // hlen returns Reply<long long>
    auto hlen_r = co_await redis.hlen(user_key);
    qb::io::cout() << "Hash now has " << hlen_r.result() << " fields" << std::endl;

    // Clean up
    (void) (co_await redis.del(user_key));
    qb::io::cout() << "Hash operations completed successfully!" << std::endl;

    co_return;
}

int
main() {
    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_hash_operations(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    return 0;
}
