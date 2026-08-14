/**
 * @file examples/qbm/redis/example4_async_operations.cpp
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
 *     -   Tracks the number of completed operations and sends a `WorkCompletedEvent`
 *         to the `MainActor` when a target count is reached.
 *     -   Handles a `ShutdownEvent` for cleanup and termination.
 * 2.  `MainActor`:
 *     -   Creates an instance of `RedisWorkerActor` (using `addRefActor`).
 *     -   Sends a configurable number of `RedisDataEvent`s to the worker actor.
 *     -   Waits for the `WorkCompletedEvent` from the worker.
 *     -   After receiving completion, schedules its own termination.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::io::async::task<bool>` onInit() coroutine pattern.
 * - `co_await client.connect()` — async connection.
 * - `co_await client.set/get/incr/del()` — coroutine commands.
 * - `qb::redis::Reply<T>`: `ok()` and `result()`.
 * - Two ways of waiting, and when each is correct: `spawn(...)` + `co_await ctx.sleep(d)` + a
 *   self-addressed tick event when the wait must end in a call ON THE ACTOR, and a bare
 *   `qb::io::async::callback(fn, d)` when the body captures nothing from it. The difference is
 *   lifetime, not style — see `MainActor::on(WorkCompletedEvent&)`.
 * - Actor communication (`push`, `addRefActor`, `spawn`).
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <qbm/redis/redis.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qb/string.h>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// Custom events for our example
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

struct WorkCompletedEvent : qb::Event {
    int operations_completed;
    explicit WorkCompletedEvent(int completed)
        : operations_completed(completed) {}
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

    RedisDataEvent(std::string_view k, std::string v)
        : key(k)
        , value(std::make_shared<std::string>(std::move(v))) {}
};

// Actor that performs Redis operations using the coroutine API
class RedisWorkerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    int                    _completed_operations = 0;
    int                    _target_operations;
    qb::ActorId            _coordinator_id;

public:
    RedisWorkerActor(int target_ops = 5, qb::ActorId coordinator = qb::ActorId())
        : _target_operations(target_ops)
        , _coordinator_id(coordinator) {}

    // onInit is now a coroutine — co_await the connection, co_return the result
    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "RedisWorkerActor initialized. Will process " << _target_operations << " operations." << std::endl;

        // Register for events before the first co_await
        registerEvent<RedisDataEvent>(*this);
        registerEvent<ShutdownEvent>(*this);

        cout << "Connecting to Redis..." << std::endl;

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "Failed to connect to Redis" << std::endl;
            co_return false;
        }

        cout << "Redis connection successful!" << std::endl;

        // Clean up existing keys with prefix 'async:'
        auto del_result = co_await _redis.del("async:counter");
        (void) del_result;
        cout << "Cleaned up existing keys with prefix 'async:'" << std::endl;

        // Initialize a counter for our example
        if (!(co_await _redis.set("async:counter", "0")).ok()) {
            qb::io::cerr() << "Failed to initialize counter" << std::endl;
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
        spawn([this, key, value](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << "Storing data at key: " << key << std::endl;

            // SET operation
            if (!(co_await _redis.set(key, value)).ok()) {
                qb::io::cerr() << "SET failed for key: " << key << std::endl;
                co_return;
            }
            cout << "Data stored successfully at key: " << key << std::endl;

            // INCR counter atomically
            auto incr_r = co_await _redis.incr("async:counter");
            if (incr_r.ok()) {
                cout << "Counter incremented to: " << incr_r.result() << std::endl;
            }

            // Track operation completion
            _completed_operations++;
            cout << "Completed " << _completed_operations << " of " << _target_operations << " operations" << std::endl;

            // GET the current value to demonstrate retrieval
            auto get_r = co_await _redis.get(key);
            if (get_r.ok() && get_r.result().has_value()) {
                cout << "Current value of " << key << ": " << *get_r.result() << std::endl;
            }

            // If we've reached our target, notify coordinator and self-shutdown
            if (_completed_operations >= _target_operations) {
                cout << "Reached target number of operations, notifying coordinator" << std::endl;

                if (_coordinator_id != qb::ActorId()) {
                    push<WorkCompletedEvent>(_coordinator_id, _completed_operations);
                }

                push<ShutdownEvent>(id());
            }
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

            cout << "RedisWorkerActor shutting down" << std::endl;
            kill();
        });
    }
};

// Main coordinator actor that creates worker and sends data
class MainActor : public qb::Actor {
private:
    qb::ActorId _worker_id;
    int         _target_operations = 5;
    bool        _work_completed    = false;

public:
    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "MainActor initialized" << std::endl;

        // Register for events before any co_await
        registerEvent<qb::KillEvent>(*this);
        registerEvent<WorkCompletedEvent>(*this);
        registerEvent<SelfShutdownTick>(*this);

        // Create worker actor on the same core, passing our ID so it can notify us
        auto worker_handle = addRefActor<RedisWorkerActor>(_target_operations, id());

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
            push<RedisDataEvent>(_worker_id, key, value);

            // This one may stay a bare `callback(fn, d)`: the body captures `i` by value and
            // touches no actor state, so it is correct whether or not this actor still exists
            // when the timer fires. Compare `on(WorkCompletedEvent&)` below, where it would not be.
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
        cout << "MainActor: Received work completed notification. " << event.operations_completed << " operations processed." << std::endl;

        _work_completed = true;

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

    auto main_actor_id = engine.addActor<MainActor>(0);
    if (main_actor_id == 0) {
        qb::io::cerr() << "Failed to create main actor" << std::endl;
        return 1;
    }

    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;

    engine.join();

    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Async Operations Example completed" << std::endl;

    return 0;
}
