/**
 * @file examples/core/example4_lifecycle.cpp
 * @example Actor Lifecycle Management and Supervision
 * 
 * @brief This example showcases advanced actor lifecycle management, including
 * starting, stopping, monitoring, and coordinated shutdown of actors using a
 * supervisor-worker pattern.
 *
 * @details
 * The system features:
 * 1.  `WorkerActor`:
 *     -   Represents a unit of work that can be started, monitored, and stopped.
 *     -   Waits for a `StartWorkEvent` to begin its periodic processing task (managed by `qb::ICallback`).
 *     -   Responds to `StatusRequestEvent` with its current progress (e.g., items processed).
 *     -   Handles `ShutdownRequestEvent` by performing cleanup (e.g., unregistering its callback)
 *         and then terminating itself using `kill()`.
 *     -   Explicitly handles `qb::KillEvent` for graceful termination.
 * 2.  `SupervisorActor`:
 *     -   Manages a group of `WorkerActor` instances.
 *     -   Upon initialization, sends `StartWorkEvent` to all its workers.
 *     -   Periodically uses `qb::ICallback` to send `StatusRequestEvent` to each worker and
 *         receives `StatusResponseEvent`s.
 *     -   After a predefined number of status checks, or if it receives a `qb::KillEvent`,
 *         it initiates a coordinated shutdown by sending `ShutdownRequestEvent` to all workers
 *         before terminating itself.
 *  
 * This example highlights how to manage actor states, interactions, and ensure
 * graceful startup and shutdown procedures in a multi-actor system.
 *
 * QB Features Demonstrated:
 * - Supervisor-Worker Pattern: `SupervisorActor` managing `WorkerActor`s.
 * - Coordinated Lifecycle: Starting workers with `StartWorkEvent`, stopping with `ShutdownRequestEvent`.
 * - Status Polling: `StatusRequestEvent` and `StatusResponseEvent` for monitoring.
 * - Event System: Multiple custom events for specific lifecycle and control actions.
 * - System Event Handling: Explicit handling of `qb::KillEvent`.
 * - Callback Management: `qb::ICallback`, `registerCallback()`, `onCallback()`, and `unregisterCallback()`.
 * - Actor Communication: `push<EventType>(...)`, `event.getSource()`.
 * - Engine and Actor Management: `qb::Main`, `engine.addActor<ActorType>()`, `kill()`.
 * - Thread-Safe I/O: `qb::io::cout()`.
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>

// Helper function to get current timestamp string
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%H:%M:%S");
    return ss.str();
}

// Step 1: Define lifecycle events
struct StartWorkEvent : public qb::Event {};
struct StatusRequestEvent : public qb::Event {};
struct StatusResponseEvent : public qb::Event {
    int processed_count;
    explicit StatusResponseEvent(int count) : processed_count(count) {}
};
struct ShutdownRequestEvent : public qb::Event {};

// Step 2: Create a worker actor with periodic processing
class WorkerActor : public qb::Actor, public qb::ICallback {
private:
    int _processed_count = 0;
    bool _running = false;
    
public:
    bool onInit() override {
        // Register event handlers
        registerEvent<StartWorkEvent>(*this);
        registerEvent<StatusRequestEvent>(*this);
        registerEvent<ShutdownRequestEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Initialized, waiting for start signal\n";
        return true;
    }
    
    void on(StartWorkEvent const&) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Received start command, beginning work\n";
        
        // Register the callback to start periodic processing
        _running = true;
        registerCallback(*this);
    }
    
    void on(StatusRequestEvent const& event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Status requested, processed " << _processed_count << " items\n";
        
        // Reply to the requester with our status
        push<StatusResponseEvent>(event.getSource(), _processed_count);
    }
    
    void on(ShutdownRequestEvent const&) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Shutdown requested, stopping work\n";
        
        // Unregister the callback to stop periodic processing
        if (_running) {
            unregisterCallback(*this);
            _running = false;
        }
        
        // Allow some time for cleanup
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Performing cleanup before shutdown\n";
        
        // In a real system, you would perform cleanup here
        
        // Kill the actor
        kill();
    }
    
    void on(qb::KillEvent const&) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Kill event received, terminating\n";
        
        // Ensure callback is unregistered
        if (_running) {
            unregisterCallback(*this);
            _running = false;
        }
        
        // Call the default implementation which will terminate the actor
        kill();
    }
    
    void onCallback() override {
        // This is called periodically while the callback is registered
        _processed_count++;
        
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id()
                  << ": Processing item #" << _processed_count << "\n";
        
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

// Step 3: Create a supervisor actor to manage workers
class SupervisorActor : public qb::Actor, public qb::ICallback {
private:
    std::vector<qb::ActorId> _worker_ids;
    int _status_check_count = 0;
    const int _max_checks = 5;
    
public:
    explicit SupervisorActor(std::vector<qb::ActorId> worker_ids) 
        : _worker_ids(std::move(worker_ids)) {}
    
    bool onInit() override {
        // Register event handlers
        registerEvent<StatusResponseEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerCallback(*this);
        
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Initialized, managing "
                  << _worker_ids.size() << " workers\n";
        
        // Start all workers
        for (const auto& worker_id : _worker_ids) {
            qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Starting worker "
                      << worker_id << "\n";
            push<StartWorkEvent>(worker_id);
        }
        
        return true;
    }
    
    void on(StatusResponseEvent const& event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Received status from worker: "
                  << event.getSource() << ", processed count: " << event.processed_count << "\n";
    }
    
    void on(qb::KillEvent const&) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Kill event received, shutting down all workers\n";
        
        // Send shutdown request to all workers
        for (const auto& worker_id : _worker_ids) {
            push<ShutdownRequestEvent>(worker_id);
        }
        
        // Unregister callback and terminate
        unregisterCallback(*this);
        kill();
    }
    
    void onCallback() override {
        // Periodically check worker status
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Checking status of all workers\n";
        
        for (const auto& worker_id : _worker_ids) {
            push<StatusRequestEvent>(worker_id);
        }
        
        _status_check_count++;
        
        // After a certain number of checks, initiate shutdown
        if (_status_check_count >= _max_checks) {
            qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Reached max status checks, initiating shutdown\n";
            
            // Send shutdown request to all workers
            for (const auto& worker_id : _worker_ids) {
                push<ShutdownRequestEvent>(worker_id);
            }
            
            // Unregister callback and terminate
            unregisterCallback(*this);
            kill();
        }
    }
};

int main() {
    // Create the main QB engine
    qb::Main engine;
    
    // Create 3 workers on core 0
    std::vector<qb::ActorId> worker_ids;
    for (int i = 0; i < 3; i++) {
        worker_ids.push_back(engine.addActor<WorkerActor>(0));
    }
    
    // Create supervisor on core 0
    engine.addActor<SupervisorActor>(0, worker_ids);
    
    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: Starting QB engine\n";
    engine.start();
    
    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: Waiting for actors to complete\n";
    engine.join();
    
    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: All actors have terminated, exiting\n";
    return 0;
} 