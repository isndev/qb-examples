/**
 * @file examples/qbm/redis/example4_async_operations.cpp
 * @example qbm-redis: Asynchronous Operations within QB Actors (Conceptual)
 *
 * @brief This example illustrates how `qbm-redis` can be integrated into a QB actor
 * system. It features a worker actor performing Redis operations based on events
 * from a main/coordinator actor.
 *
 * @details
 * The system consists of:
 * 1.  `RedisWorkerActor`:
 *     -   Connects to a Redis server upon initialization.
 *     -   Receives `RedisDataEvent` (containing a key and value) and performs Redis operations:
 *         -   `_redis.set(key, value)`
 *         -   `_redis.incr("async:counter")`
 *         -   `_redis.get(key)` (to demonstrate retrieving the set value)
 *     -   **Important Note**: In this specific example, the Redis commands (`set`, `incr`, `get`)
 *         are invoked synchronously within the actor's event handler. In a production QB actor system,
 *         this would block the actor's thread and is generally an anti-pattern. `qbm-redis`
 *         provides asynchronous versions of commands (e.g., `cmd_async()` or methods that
 *         accept a callback and return `Reply<T>`), which should be used to maintain actor responsiveness.
 *         This example serves as a basic structural guide for actor-Redis interaction rather than
 *         a best-practice for non-blocking Redis calls from actors.
 *     -   Tracks the number of completed operations and sends a `WorkCompletedEvent` to the
 *         `MainActor` when a target count is reached.
 *     -   Handles a `ShutdownEvent` for cleanup and termination.
 * 2.  `MainActor`:
 *     -   Creates an instance of `RedisWorkerActor` (using `addRefActor`).
 *     -   Sends a configurable number of `RedisDataEvent`s to the worker actor to trigger Redis operations.
 *     -   Waits for the `WorkCompletedEvent` from the worker.
 *     -   After receiving completion, it requests the worker to shut down (via `ShutdownEvent`)
 *         and then schedules its own termination.
 *
 * The `qb::Main` engine manages these actors.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::Actor`, `qb::Main`, `qb::Event`.
 * - `qb::redis::tcp::client` usage within an actor.
 * - `client.connect()`, `client.set()`, `client.get()`, `client.incr()`, `client.del()` (synchronous versions).
 * - Actor communication (`push`, `addRefActor`).
 * - `qb::io::async::callback` for delayed actions in `MainActor`.
 * - `qb::io::cout()`.
 *
 * Key Takeaway: While showing Redis client use in an actor, for true non-blocking behavior,
 * the asynchronous API of `qbm-redis` (methods with callbacks or `_async` suffix) should be used
 * instead of the direct synchronous calls made from the worker's event handler in this example.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <iostream>
#include <chrono>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// Custom events for our example
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

struct WorkCompletedEvent : qb::Event {
    int operations_completed;
    explicit WorkCompletedEvent(int completed) : operations_completed(completed) {}
};

struct RedisDataEvent : qb::Event {
    std::string key;
    std::string value;

    RedisDataEvent(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)) {}
};

// Actor that performs asynchronous Redis operations
class RedisWorkerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    bool _connected = false;
    int _completed_operations = 0;
    int _target_operations;
    qb::ActorId _coordinator_id;  // ID of the actor that created this worker
    
public:
    RedisWorkerActor(int target_ops = 5, qb::ActorId coordinator = qb::ActorId())
        : _target_operations(target_ops), _coordinator_id(coordinator) {}

    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "RedisWorkerActor initialized. Will process " 
             << _target_operations << " operations." << std::endl;
        
        // Register for events
        registerEvent<RedisDataEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Make async connection to Redis
        cout << "Connecting to Redis asynchronously..." << std::endl;
        
        if (!_redis.connect()) {
            qb::io::cerr() << "Failed to initial Redis connection" << std::endl;
            return false;
        }
        
        _connected = true;
        cout << "Redis connection successful!" << std::endl;
            
        // Start with a clean slate
        _redis.del("async:*");
        cout << "Cleaned up existing keys with prefix 'async:'" << std::endl;
        
        // Initialize a counter for our example
        _redis.set("async:counter", "0");
        cout << "Initialized counter to 0" << std::endl;
        
        return true;
    }

    void on(const RedisDataEvent& event) {
        auto cout = qb::io::cout();
        std::string key = event.key;
        
        // Store data asynchronously
        cout << "Storing data asynchronously at key: " << key << std::endl;
        
        _redis.set(key, event.value);
        cout << "Data stored successfully at key: " << key << std::endl;
            
        // Increment a counter atomically
        auto incr_result = _redis.incr("async:counter");
        cout << "Counter incremented to: " << incr_result << std::endl;
        
        // Track operation completion
        _completed_operations++;
        cout << "Completed " << _completed_operations << " of " 
             << _target_operations << " operations" << std::endl;
            
        // Demonstrate parallel Redis operations - get the current value
        auto get_result = _redis.get(key);
        if (get_result.has_value()) {
            cout << "Current value of " << key << ": " 
                 << *get_result << std::endl;
        }
        
        // If we've reached our target, notify coordinator and request shutdown
        if (_completed_operations >= _target_operations) {
            cout << "Reached target number of operations, notifying coordinator" << std::endl;
            
            // First, notify the coordinator that we're done
            if (_coordinator_id != qb::ActorId()) {
                push<WorkCompletedEvent>(_coordinator_id, _completed_operations);
            }
            
            // Then initiate our own shutdown
            push<ShutdownEvent>(id());
        }
    }

    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << "Received shutdown request" << std::endl;
            
        // Get final stats
        auto get_result = _redis.get("async:counter");
        if (get_result.has_value()) {
            cout << "Final counter value: " << *get_result << std::endl;
        }
            
        // All done, terminate this actor
        cout << "RedisWorkerActor shutting down" << std::endl;
        kill();
    }
};

// Main coordinator actor that creates worker and sends data
class MainActor : public qb::Actor {
private:
    qb::ActorId _worker_id;
    int _target_operations = 5;
    RedisWorkerActor* _worker_ptr = nullptr;  // Store a direct reference to the worker
    bool _work_completed = false;

public:
    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "MainActor initialized" << std::endl;
        
        // Register for events
        registerEvent<qb::KillEvent>(*this);
        registerEvent<WorkCompletedEvent>(*this);
        
        // Create worker actor on the same core for simplicity
        // Pass our ID so the worker can notify us when it's done
        _worker_ptr = addRefActor<RedisWorkerActor>(_target_operations, id());
        
        if (!_worker_ptr) {
            qb::io::cerr() << "Failed to create worker actor" << std::endl;
            return false;
        }
        
        _worker_id = _worker_ptr->id();  // Store worker's ID for messaging
        
        cout << "Created RedisWorkerActor with ID: " << _worker_id << std::endl;
        
        // Schedule some data operations
        for (int i = 1; i <= _target_operations; i++) {
            std::string key = "async:data:" + std::to_string(i);
            std::string value = "This is async test data #" + std::to_string(i);
            
            cout << "Sending data operation " << i << " to worker" << std::endl;
            push<RedisDataEvent>(_worker_id, key, value);
            
            // Small delay between operations to make output readable
            qb::io::async::callback([this, i]() {
                auto cout = qb::io::cout();
                cout << "MainActor: scheduled operation " << i << " sent" << std::endl;
            }, 0.1 * i);
        }
        
        return true;
    }

    // Handle completion notification from worker
    void on(const WorkCompletedEvent& event) {
        auto cout = qb::io::cout();
        cout << "MainActor: Received work completed notification. "
             << event.operations_completed << " operations processed." << std::endl;
        
        _work_completed = true;
        
        // Schedule our own termination with a small delay to allow
        // any remaining callbacks to complete
        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "MainActor: All work is done, shutting down..." << std::endl;
            kill();
        }, 1.0);  // Wait 1 second before shutting down
    }

    void on(const qb::KillEvent&) {
        auto cout = qb::io::cout();
        cout << "MainActor shutting down" << std::endl;
        kill();
    }
};

int main() {
    // Initialize the async system
    qb::io::async::init();
    auto cout = qb::io::cout();
    
    cout << "Starting Redis Async Operations Example" << std::endl;
    
    // Create the engine
    qb::Main engine;
    
    // Add the main actor to core 0
    auto main_actor_id = engine.addActor<MainActor>(0);
    if (main_actor_id == 0) {  // 0 is an invalid actor ID
        qb::io::cerr() << "Failed to create main actor" << std::endl;
        return 1;
    }
    
    // Start the engine (non-blocking)
    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;
    
    // Wait for engine to stop (when all actors terminate)
    engine.join();
    
    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Async Operations Example completed" << std::endl;
    
    return 0;
} 