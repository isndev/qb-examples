/**
 * @file examples/06-modules/redis/10-cache-actor.cpp
 * @tier 06-modules
 * @teaches Redis inside an actor rather than beside one: the client is a member, the connect happens
 *          in a coroutine onInit, and every command is awaited from a handler that never blocks.
 * @demonstrates qb::redis::tcp::client, spawn, qb::ScopedCoroContext, ctx.sleep,
 *               qb::io::async::callback, qb::io::async::task<void>, registerEvent<E>, qb::KillEvent
 * @prerequisites 06-modules/redis/01-connect, 03-coroutines/02-actor-coroutines
 * @expect "Redis connection successful!"
 * @expect "Data stored successfully at key: "
 * @example qbm-redis: Asynchronous Operations within QB Actors (Coroutine API)
 *
 * @brief This example illustrates how `qbm-redis` can be integrated into a QB actor
 * system using the modern coroutine API. It features a worker actor performing Redis
 * operations based on events from a main/coordinator actor.
 *
 * @details
 * The system consists of:
 * 1.  `RedisWorkerActor`:
 *     -   Connects to a Redis server upon initialization via `co_await _redis.connect()`.
 *     -   `onInit()` is a `qb::io::async::task<bool>` coroutine.
 *     -   Receives `RedisDataEvent` (containing a key and value) and spawns a coroutine
 *         to perform Redis operations:
 *         -   `co_await _redis.set(key, value)`
 *         -   `co_await _redis.incr("async:counter")`
 *         -   `co_await _redis.get(key)` (to retrieve the set value)
 *     -   Sends one `WorkCompletedEvent` per request, including failures.
 *     -   Handles a `ShutdownEvent` for cleanup and termination.
 * 2.  `MainActor`:
 *     -   Creates an instance of `RedisWorkerActor` (using `addRefActor`).
 *     -   Sends five `RedisDataEvent`s to the worker actor.
 *     -   Counts all five results, then requests worker cleanup.
 *     -   After cleanup, schedules its own termination and reports the result to main().
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::io::async::task<bool>` onInit() coroutine pattern.
 * - `co_await client.connect()` — async connection.
 * - `co_await client.set/get/incr/del()` — coroutine commands.
 * - `qb::redis::Reply<T>`: `ok()` and `result()`.
 * - Two ways of waiting, and when each is correct: `spawn(...)` + `co_await ctx.sleep(d)` + a
 *   self-addressed tick event when the wait must end in a call ON THE ACTOR, and a bare
 *   `qb::io::async::callback(fn, d)` when the body captures nothing from it. The difference is
 *   lifetime, not style — see `MainActor::on(WorkerStoppedEvent&)`.
 * - Actor communication (`push`, `addRefActor`, `spawn`).
 */

#include <chrono>
#include <atomic>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <qbm/redis/redis.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qb/string.h>

// The default keeps the corpus unchanged; the override lets the regression test use a
// disposable Redis instance instead of the developer's server.
qb::io::uri
redis_uri() {
    const char *configured = std::getenv("QB_EXAMPLE_REDIS_URI");
    return qb::io::uri{std::string{configured ? configured : "tcp://localhost:6379"}};
}

// Custom events for our example
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

struct WorkCompletedEvent : qb::Event {
    bool succeeded;
    explicit WorkCompletedEvent(bool ok)
        : succeeded(ok) {}
};

struct WorkerStoppedEvent : qb::Event {
    bool cleanup_succeeded;
    explicit WorkerStoppedEvent(bool ok)
        : cleanup_succeeded(ok) {}
};

/**
 * @brief Self-addressed wake-up: "the shutdown grace period has elapsed".
 *
 * The delay is served by `spawn(...)` + `co_await ctx.sleep(d)`, which the actor's cancellation
 * scope owns; the coroutine then pushes this, and the handler — which runs only on a live actor
 * — does the work. `qb::io::async::callback([this]{ kill(); }, d)` would instead leave a timer
 * owned by the event loop, firing `this->kill()` at whatever now occupies that memory.
 */
struct SelfShutdownTick : qb::Event {};

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
//
struct RedisDataEvent : qb::Event {
    qb::string<64>               key;
    std::shared_ptr<std::string> value; // a stored value has no bound: box it
    int                          slot;

    RedisDataEvent(std::string_view k, std::string v, int index)
        : key(k)
        , value(std::make_shared<std::string>(std::move(v)))
        , slot(index) {}
};

// Actor that performs Redis operations using the coroutine API
class RedisWorkerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{redis_uri()};
    std::array<bool, 5>    _stored{};
    qb::ActorId            _coordinator_id;

public:
    explicit RedisWorkerActor(qb::ActorId coordinator)
        : _coordinator_id(coordinator) {}

    // onInit is now a coroutine — co_await the connection, co_return the result
    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "RedisWorkerActor initialized." << std::endl;

        // Register for events before the first co_await
        registerEvent<RedisDataEvent>(*this);
        registerEvent<ShutdownEvent>(*this);

        cout << "Connecting to Redis..." << std::endl;

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "Failed to connect to Redis" << std::endl;
            push<WorkerStoppedEvent>(_coordinator_id, false);
            co_return false;
        }

        cout << "Redis connection successful!" << std::endl;

        // Clear the example's counter from any previous interrupted run.
        auto del_result = co_await _redis.del("async:counter");
        if (!del_result.ok()) {
            qb::io::cerr() << "Failed to clear counter" << std::endl;
            push<WorkerStoppedEvent>(_coordinator_id, false);
            co_return false;
        }
        cout << "Cleared existing async:counter" << std::endl;

        // Initialize a counter for our example
        if (!(co_await _redis.set("async:counter", "0")).ok()) {
            qb::io::cerr() << "Failed to initialize counter" << std::endl;
            push<WorkerStoppedEvent>(_coordinator_id, false);
            co_return false;
        }
        cout << "Initialized counter to 0" << std::endl;

        co_return true;
    }

    void
    on(const RedisDataEvent &event) {
        // Spawn a coroutine to handle async Redis operations for this event
        std::string key   = event.key.c_str();
        std::string value = event.value ? *event.value : std::string{};
        int         slot  = event.slot;

        // Member access after `co_await _redis...` is safe HERE, and the reason is OWNERSHIP,
        // not `spawn`'s scope. `spawn` cancels only at scope-routed suspensions (`ctx.sleep`,
        // `ctx.cancellation_point`, `ctx.until_cancelled`, `ctx.cancellable`); a qbm command
        // awaiter registers nothing with the token, so `kill()` does not reach this body.
        // What protects it is that `_redis` is a MEMBER: `~Actor` destroys the client together
        // with its pending-reply queue, the reply callback is discarded UNINVOKED, and this
        // coroutine simply never resumes — measured by killing an actor parked on a 3 s BRPOP:
        // no resume, ASan silent. The cost is an orphaned frame, not a use-after-free. Give
        // the client a lifetime that outlives the actor (a `shared_ptr`, a service actor) and
        // this exact body becomes one, because then the reply DOES arrive.
        spawn([this, key, value, slot](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << "Storing data at key: " << key << std::endl;

            // SET operation
            if (!(co_await _redis.set(key, value)).ok()) {
                qb::io::cerr() << "SET failed for key: " << key << std::endl;
                push<WorkCompletedEvent>(_coordinator_id, false);
                co_return;
            }
            _stored[slot] = true;
            cout << "Data stored successfully at key: " << key << std::endl;

            // INCR counter atomically
            auto incr_r = co_await _redis.incr("async:counter");
            if (incr_r.ok()) {
                cout << "Counter incremented to: " << incr_r.result() << std::endl;
            } else {
                qb::io::cerr() << "INCR failed for key: " << key << std::endl;
            }

            // GET the current value to demonstrate retrieval
            auto       get_r   = co_await _redis.get(key);
            const bool read_ok = get_r.ok() && get_r.result().has_value() && *get_r.result() == value;
            if (read_ok) {
                cout << "Current value of " << key << ": " << *get_r.result() << std::endl;
            } else {
                qb::io::cerr() << "GET failed for key: " << key << std::endl;
            }

            push<WorkCompletedEvent>(_coordinator_id, incr_r.ok() && read_ok);
        });
    }

    void
    on(const ShutdownEvent &) {
        // Spawn a coroutine to fetch final stats before killing
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << "Received shutdown request" << std::endl;

            auto get_r = co_await _redis.get("async:counter");
            if (get_r.ok() && get_r.result().has_value()) {
                cout << "Final counter value: " << *get_r.result() << std::endl;
            }

            // Delete only successful SETs. A restricted ACL may deny async:data:* entirely,
            // while still allowing cleanup of the initialized counter.
            bool cleanup_ok    = get_r.ok() && get_r.result().has_value();
            int  removed_count = 0;
            for (std::size_t slot = 0; slot < _stored.size(); ++slot) {
                if (!_stored[slot])
                    continue;
                auto removed = co_await _redis.del("async:data:" + std::to_string(slot + 1));
                cleanup_ok &= removed.ok();
                if (removed.ok())
                    removed_count += static_cast<int>(removed.result());
            }
            auto removed_counter = co_await _redis.del("async:counter");
            cleanup_ok &= removed_counter.ok();
            if (removed_counter.ok())
                removed_count += static_cast<int>(removed_counter.result());
            cout << "Deleted " << removed_count << " key(s) written by this run" << std::endl;

            cout << "RedisWorkerActor shutting down" << std::endl;
            push<WorkerStoppedEvent>(_coordinator_id, cleanup_ok);
            kill();
        });
    }
};

// Main coordinator actor that creates worker and sends data
class MainActor : public qb::Actor {
private:
    qb::ActorId        _worker_id;
    int                _target_operations = 5;
    int                _completed_results = 0;
    bool               _all_work_ok       = true;
    bool               _shutdown_started  = false;
    bool               _worker_stopped    = false;
    std::atomic<bool> &_run_ok;

public:
    explicit MainActor(std::atomic<bool> &run_ok)
        : _run_ok(run_ok) {}

    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "MainActor initialized" << std::endl;

        // Register for events before any co_await
        registerEvent<qb::KillEvent>(*this);
        registerEvent<WorkCompletedEvent>(*this);
        registerEvent<WorkerStoppedEvent>(*this);
        registerEvent<SelfShutdownTick>(*this);

        // Create worker actor on the same core, passing our ID so it can notify us
        auto worker_handle = addRefActor<RedisWorkerActor>(id());

        if (!worker_handle.valid()) {
            qb::io::cerr() << "Failed to create worker actor" << std::endl;
            co_return false;
        }

        _worker_id = worker_handle.id();
        cout << "Created RedisWorkerActor with ID: " << _worker_id << std::endl;

        // Schedule data operations with small delays to make output readable
        for (int i = 1; i <= _target_operations; i++) {
            std::string key   = "async:data:" + std::to_string(i);
            std::string value = "This is async test data #" + std::to_string(i);

            cout << "Sending data operation " << i << " to worker" << std::endl;
            push<RedisDataEvent>(_worker_id, key, value, i - 1);

            // This one may stay a bare `callback(fn, d)`: the body captures `i` by value and
            // touches no actor state, so it is correct whether or not this actor still exists
            // when the timer fires. Compare `on(WorkerStoppedEvent&)` below, where it would not be.
            qb::io::async::callback(
                [i]() {
                    auto cout2 = qb::io::cout();
                    cout2 << "MainActor: scheduled operation " << i << " sent" << std::endl;
                },
                std::chrono::milliseconds(100 * i));
        }

        co_return true;
    }

    // Handle completion notification from worker
    void
    on(const WorkCompletedEvent &event) {
        auto cout = qb::io::cout();
        _all_work_ok &= event.succeeded;
        ++_completed_results;
        cout << "MainActor: Received work result " << _completed_results << " of " << _target_operations
             << (event.succeeded ? " (ok)" : " (failed)") << std::endl;
        if (_completed_results == _target_operations && !_shutdown_started) {
            _shutdown_started = true;
            push<ShutdownEvent>(_worker_id);
        }
    }

    void
    on(const WorkerStoppedEvent &event) {
        if (_worker_stopped)
            return;
        _worker_stopped   = true;
        _shutdown_started = true;
        _run_ok.store(_completed_results == _target_operations && _all_work_ok && event.cleanup_succeeded);
        // Schedule our own termination with a small delay. The wait belongs to this actor's
        // cancellation scope; the `kill()` happens in the handler, which only ever runs on a
        // live actor. `callback([this]{ kill(); }, 1s)` would leave a loop-owned timer holding a
        // raw `this`, and nothing cancels it when the actor goes away.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(1));
            ctx.template push<SelfShutdownTick>();
        });
    }

    void
    on(const SelfShutdownTick &) {
        auto cout = qb::io::cout();
        cout << "MainActor: All work is done, shutting down..." << std::endl;
        kill();
    }

    void
    on(const qb::KillEvent &) {
        auto cout = qb::io::cout();
        cout << "MainActor shutting down" << std::endl;
        kill();
    }
};

int
main() {
    qb::io::async::init();
    auto cout = qb::io::cout();

    cout << "Starting Redis Async Operations Example" << std::endl;

    qb::Main engine;

    std::atomic<bool> run_ok{false};
    auto              main_actor_id = engine.addActor<MainActor>(0, std::ref(run_ok));
    if (main_actor_id == 0) {
        qb::io::cerr() << "Failed to create main actor" << std::endl;
        return 1;
    }

    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;

    engine.join();

    cout << "Engine stopped, all actors terminated" << std::endl;
    if (!run_ok.load()) {
        qb::io::cerr() << "Redis Async Operations Example failed" << std::endl;
        return 1;
    }
    cout << "Redis Async Operations Example completed" << std::endl;

    return 0;
}
