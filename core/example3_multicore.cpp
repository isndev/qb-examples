/**
 * @file examples/core/example3_multicore.cpp
 * @example Multi-Core Actor Distribution and Event Broadcasting
 * 
 * @brief This example illustrates how to distribute actors across multiple CPU cores
 * and how to use broadcast events for system-wide or core-specific notifications.
 * It simulates a workload distributed by a dispatcher to worker actors running on different cores.
 *
 * @details
 * The system comprises:
 * 1.  `WorkerActor`:
 *     -   Deployed on multiple CPU cores (configurable, up to `std::thread::hardware_concurrency()`).
 *     -   Handles three types of events: `HighPriorityEvent`, `StandardEvent`, and `LowPriorityEvent`.
 *     -   Simulates different processing times based on event priority.
 *     -   Receives `SystemNotificationEvent`s broadcast by the dispatcher.
 *     -   Terminates after processing a predefined number of events.
 * 2.  `DispatcherActor`:
 *     -   Runs on a specific core (core 0 in this example).
 *     -   Uses `qb::ICallback` to periodically create and dispatch work events to `WorkerActor`s
 *         in a round-robin fashion.
 *     -   Periodically broadcasts `SystemNotificationEvent`s to all actors on core 0 using `push<SystemNotificationEvent>(qb::BroadcastId(0), ...)`.
 *         (Note: Workers are on other cores but also register for this event. For true all-worker broadcast, iterating known worker IDs or using `broadcast<T>()` would be typical).
 *     -   Terminates after dispatching all planned work.
 *
 * The `qb::Main` engine manages the actors and their assignment to cores.
 *
 * QB Features Demonstrated:
 * - Multi-Core Actor Assignment: `engine.addActor<ActorType>(core_id, args...)`.
 * - Actor Concurrency: Multiple `WorkerActor`s processing events in parallel on different cores.
 * - Event System: Multiple custom event types (`HighPriorityEvent`, `StandardEvent`, `LowPriorityEvent`, `SystemNotificationEvent`).
 * - Event Handling: `onInit()`, `registerEvent<EventType>()`, `on(EventType& event)`.
 * - Message Sending: `push<EventType>(destination_actor_id, args...)`.
 * - Broadcast Messaging: `push<EventType>(qb::BroadcastId(core_id), args...)` for core-specific broadcasts. (Actual system-wide broadcast is `broadcast<T>(args...)`).
 * - Actor Lifecycle: `kill()` for self-termination.
 * - Periodic Tasks: `qb::ICallback`, `registerCallback()`, `onCallback()`.
 * - Core Information: `getIndex()` to retrieve the actor's current core ID.
 * - Engine Management: `qb::Main`, `std::thread::hardware_concurrency()`.
 * - Thread-Safe I/O: `qb::io::cout()`.
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>

// Define event types with different priorities
struct HighPriorityEvent : public qb::Event {
    int value;
    explicit HighPriorityEvent(int val) : value(val) {}
};

struct StandardEvent : public qb::Event {
    int value;
    explicit StandardEvent(int val) : value(val) {}
};

struct LowPriorityEvent : public qb::Event {
    int value;
    explicit LowPriorityEvent(int val) : value(val) {}
};

// Broadcast event for system-wide notifications
struct SystemNotificationEvent : public qb::Event {
    std::string message;
    explicit SystemNotificationEvent(const std::string& msg) : message(msg) {}
};

// Worker actor that processes events of different priorities
class WorkerActor : public qb::Actor {
private:
    int _processed_standard = 0;
    int _processed_high = 0;
    int _processed_low = 0;
    int _notifications_received = 0;
    const int _max_events = 5;
    std::string _timestamp() const {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto value = now_ms.time_since_epoch().count();
        return "[" + std::to_string(value % 10000) + "] ";
    }

public:
    WorkerActor() {
        // Register for different event types
        registerEvent<StandardEvent>(*this);
        registerEvent<HighPriorityEvent>(*this);
        registerEvent<LowPriorityEvent>(*this);
        registerEvent<SystemNotificationEvent>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Initialized on core " << getIndex() << std::endl;
        return true;
    }
    
    // Handlers for different event types
    void on(StandardEvent& event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing StandardEvent with value " << event.value << std::endl;
        // Simulate work for standard priority
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        _processed_standard++;
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Completed StandardEvent, total: " << _processed_standard << std::endl;
        
        checkCompletion();
    }
    
    void on(HighPriorityEvent& event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing HighPriorityEvent with value " << event.value << std::endl;
        // High priority tasks are processed faster
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        _processed_high++;
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Completed HighPriorityEvent, total: " << _processed_high << std::endl;
        
        checkCompletion();
    }
    
    void on(LowPriorityEvent& event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing LowPriorityEvent with value " << event.value << std::endl;
        // Low priority tasks take longer
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        _processed_low++;
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Completed LowPriorityEvent, total: " << _processed_low << std::endl;
        
        checkCompletion();
    }
    
    void on(SystemNotificationEvent& event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Received notification: " << event.message << std::endl;
        _notifications_received++;
    }
    
private:
    void checkCompletion() {
        // Check if we've processed enough events of each type
        if (_processed_standard + _processed_high + _processed_low >= _max_events * 3) {
            qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processed enough events, terminating" << std::endl;
            qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Standard: " << _processed_standard
                      << ", High: " << _processed_high 
                      << ", Low: " << _processed_low 
                      << ", Notifications: " << _notifications_received << std::endl;
            kill();
        }
    }
};

// Dispatcher actor that distributes work across cores
class DispatcherActor : public qb::Actor, public qb::ICallback {
private:
    std::vector<qb::ActorId> _workers;
    int _dispatched_events = 0;
    const int _max_events_per_worker = 5;
    const int _num_workers;
    
public:
    explicit DispatcherActor(const std::vector<qb::ActorId>& workers)
        : _workers(workers), _num_workers(workers.size()) {}
    
    bool onInit() override {
        qb::io::cout() << "DispatcherActor " << id() << ": Initialized on core " << getIndex()
                  << ", will dispatch to " << _num_workers << " workers" << std::endl;
        
        // Register a callback to start dispatching
        registerCallback(*this);
        return true;
    }
    
    void onCallback() override {
        if (_dispatched_events < _max_events_per_worker * _num_workers * 3) {
            // Round-robin dispatch
            int worker_index = (_dispatched_events / 3) % _num_workers;
            int event_type = _dispatched_events % 3;
            int value = _dispatched_events;
            
            // Send different event types based on the current count
            switch (event_type) {
                case 0:
                    qb::io::cout() << "DispatcherActor: Sending StandardEvent to worker " << _workers[worker_index] << std::endl;
                    push<StandardEvent>(_workers[worker_index], value);
                    break;
                case 1:
                    qb::io::cout() << "DispatcherActor: Sending HighPriorityEvent to worker " << _workers[worker_index] << std::endl;
                    push<HighPriorityEvent>(_workers[worker_index], value);
                    break;
                case 2:
                    qb::io::cout() << "DispatcherActor: Sending LowPriorityEvent to worker " << _workers[worker_index] << std::endl;
                    push<LowPriorityEvent>(_workers[worker_index], value);
                    break;
            }
            
            _dispatched_events++;
            
            // Every 10 events, send a broadcast notification
            if (_dispatched_events % 10 == 0) {
                std::string msg = "Progress update: " + std::to_string(_dispatched_events) + " events dispatched";
                qb::io::cout() << "DispatcherActor: Broadcasting system notification" << std::endl;
                push<SystemNotificationEvent>(qb::BroadcastId(0), msg);
            }
            
            // Schedule the next dispatch with a small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            qb::io::cout() << "DispatcherActor: All events dispatched, terminating" << std::endl;
            // Send final notification
            std::string msg = "All work completed, processed " + std::to_string(_dispatched_events) + " events";
            push<SystemNotificationEvent>(qb::BroadcastId(0), msg);
            kill();
        }
    }
};

int main() {
    // Get the number of hardware cores
    const unsigned int num_cores = std::thread::hardware_concurrency();
    // Use at least 2 cores, but no more than what's available
    const unsigned int cores_to_use = std::max(2u, std::min(4u, num_cores));
    
    qb::io::cout() << "Main: Using " << cores_to_use << " cores" << std::endl;
    
    // Create the main engine
    qb::Main engine;
    
    // Create worker actors on different cores
    std::vector<qb::ActorId> workers;
    for (unsigned int i = 0; i < cores_to_use; ++i) {
        workers.push_back(engine.addActor<WorkerActor>(i % cores_to_use));
    }
    
    // Create dispatcher actor on core 0
    engine.addActor<DispatcherActor>(0, workers);
    
    qb::io::cout() << "Main: Starting QB engine" << std::endl;
    engine.start();
    
    qb::io::cout() << "Main: Waiting for actors to complete" << std::endl;
    engine.join();
    
    qb::io::cout() << "Main: All actors have terminated, exiting" << std::endl;
    return 0;
} 