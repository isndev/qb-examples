/**
 * @file examples/core/example5_timers.cpp
 * @example Actor-Based Timer Simulation and Delayed Actions
 * 
 * @brief This example demonstrates how to implement timer-like behavior and delayed
 * actions within the QB Actor Framework using self-messaging patterns. It simulates
 * a timer manager that can schedule events to fire after specific intervals.
 *
 * @details
 * The system includes:
 * 1.  `TimerManager` Actor:
 *     -   Receives `StartTimerMsg` to schedule a new "timer".
 *     -   Manages timers internally by sending `DelayedActionMsg` events to itself.
 *         When a `DelayedActionMsg` (acting as a timer tick) is processed, it emits a
 *         `TimerFiredMsg`.
 *     -   Handles `CancelTimerMsg` to stop existing timers.
 *     -   This actor simulates timer functionality. For direct timer scheduling, QB provides
 *         `qb::io::async::callback(func, delay_seconds)`.
 * 2.  `Application` Actor:
 *     -   Interacts with the `TimerManager` to start and cancel timers.
 *     -   Receives `TimerFiredMsg` and logs them.
 *     -   Uses `DelayedActionMsg` sent to itself to sequence its own operations
 *         (e.g., stopping a timer after some time, then shutting down).
 *     -   Initiates a system-wide shutdown using `broadcast<qb::KillEvent>()`.
 * 
 * This example illustrates a pattern for managing timed or delayed operations purely
 * through actor events and internal logic, and coordinates a graceful shutdown.
 *
 * QB Features Demonstrated:
 * - Actor Communication: `push<EventType>(...)` for sending commands and events, including to self (`id()`).
 * - Event-Driven Logic: Actors reacting to various custom events (`StartTimerMsg`, `TimerFiredMsg`, etc.).
 * - Simulated Timers: Implementing timer functionality via self-directed `DelayedActionMsg` events.
 *   (Note: QB offers `qb::io::async::callback(func, delay)` for direct asynchronous delayed execution).
 * - Coordinated Shutdown: Using `broadcast<qb::KillEvent>()` for system-wide termination.
 * - Actor State Management: Actors maintain internal state for timers and application steps.
 * - Engine Management: `qb::Main`, `engine.addActor<ActorType>()`, `engine.start()`, `engine.join()`.
 * - System Event Handling: Actors registering for and handling `qb::KillEvent`.
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/event.h>
#include <qb/icallback.h>

using namespace std::chrono_literals;

// Message for scheduling a delayed action
struct DelayedActionMsg : public qb::Event {
    enum class Action {
        TIMER_FIRE,
        APP_STEP_1,
        APP_STEP_2,
        APP_STEP_3
    };
    
    Action action;
    std::string timer_name;  // For timer-specific actions
    int delay_ms;
    
    DelayedActionMsg(Action action, int delay, const std::string& timer = "")
        : action(action), timer_name(timer), delay_ms(delay) {}
};

// Message to start a timer
struct StartTimerMsg : public qb::Event {
    std::chrono::milliseconds interval;
    int repeat_count;
    std::string timer_name;
    
    StartTimerMsg(const std::chrono::milliseconds& i, int r, const std::string& name)
        : interval(i), repeat_count(r), timer_name(name) {}
};

// Message emitted when a timer fires
struct TimerFiredMsg : public qb::Event {
    std::string timer_name;
    int count;
    
    TimerFiredMsg(const std::string& name, int c)
        : timer_name(name), count(c) {}
};

// Message to cancel a timer
struct CancelTimerMsg : public qb::Event {
    std::string timer_name;
    
    CancelTimerMsg(const std::string& name)
        : timer_name(name) {}
};

// Actor that manages timers
class TimerManager : public qb::Actor {
public:
    TimerManager() {
        registerEvent<StartTimerMsg>(*this);
        registerEvent<DelayedActionMsg>(*this);
        registerEvent<CancelTimerMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    bool onInit() override {
        qb::io::cout() << "TimerManager " << id() << ": Initialized\n";
        return true;
    }
    
    void on(StartTimerMsg& msg) {
        qb::io::cout() << "Starting timer '" << msg.timer_name
                  << "' with interval " << msg.interval.count() 
                  << "ms, repeat " << msg.repeat_count << " times\n";
        
        // Store timer information for management
        TimerInfo info;
        info.name = msg.timer_name;
        info.interval = msg.interval;
        info.repeat_count = msg.repeat_count;
        info.current_count = 0;
        
        _timers[msg.timer_name] = info;
        
        // Schedule the first timer execution
        scheduleTimer(msg.timer_name);
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::TIMER_FIRE) {
            fireTimer(msg.timer_name);
        }
    }
    
    void on(CancelTimerMsg& msg) {
        auto it = _timers.find(msg.timer_name);
        if (it != _timers.end()) {
            qb::io::cout() << "Cancelling timer '" << msg.timer_name << "'\n";
            _timers.erase(it);
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "TimerManager: Shutting down\n";
        
        // Clear all timers
        _timers.clear();
        
        // Terminate self
        kill();
    }
    
private:
    void scheduleTimer(const std::string& timer_name) {
        auto it = _timers.find(timer_name);
        if (it != _timers.end()) {
            // Schedule this timer to fire after its interval
            push<DelayedActionMsg>(
                id(), 
                DelayedActionMsg::Action::TIMER_FIRE, 
                it->second.interval.count(),
                timer_name
            );
        }
    }
    
    void fireTimer(const std::string& timer_name) {
        auto it = _timers.find(timer_name);
        if (it != _timers.end()) {
            TimerInfo& info = it->second;
            info.current_count++;
            
            // Fire the timer event
            push<TimerFiredMsg>(id(), info.name, info.current_count);
            qb::io::cout() << "Timer '" << info.name
                      << "' fired, count: " << info.current_count << std::endl;
            
            // Check if we should stop the timer
            if (info.repeat_count > 0 && info.current_count >= info.repeat_count) {
                qb::io::cout() << "Timer '" << info.name << "' completed all "
                          << info.repeat_count << " repetitions\n";
                _timers.erase(it);
            } else if (_timers.find(timer_name) != _timers.end()) {
                // Reschedule the timer for its next execution
                scheduleTimer(timer_name);
            }
        }
    }

    struct TimerInfo {
        std::string name;
        std::chrono::milliseconds interval;
        int repeat_count;
        int current_count;
    };
    
    std::unordered_map<std::string, TimerInfo> _timers;
};

// Actor representing our application
class Application : public qb::Actor {
public:
    Application(qb::ActorId timer_manager_id) 
        : _timer_manager_id(timer_manager_id), _step(0) {
        registerEvent<DelayedActionMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<TimerFiredMsg>(*this);
    }

    bool onInit() override {
        qb::io::cout() << "Application starting...\n";
        
        // Start a fast timer that repeats 5 times
        push<StartTimerMsg>(_timer_manager_id, 500ms, 5, "fast_timer");
        
        // Start a slow timer that repeats indefinitely (repeat_count = 0)
        push<StartTimerMsg>(_timer_manager_id, 2000ms, 0, "slow_timer");
        
        // Schedule the first application step with a longer delay
        // to allow for observing timers in action
        push<DelayedActionMsg>(id(), DelayedActionMsg::Action::APP_STEP_1, 5000);
        
        return true;
    }
    
    // Forward timer events to console for better visualization
    void on(TimerFiredMsg& msg) {
        // We're forwarding these from TimerManager to better track the flow
        qb::io::cout() << "Application received timer event: " << msg.timer_name
                  << " fired (count: " << msg.count << ")" << std::endl;
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::APP_STEP_1) {
            qb::io::cout() << "Application stopping slow_timer...\n";
            push<CancelTimerMsg>(_timer_manager_id, "slow_timer");
            
            // Schedule the next application step with a longer delay
            // to allow fast_timer to complete all repetitions
            push<DelayedActionMsg>(id(), DelayedActionMsg::Action::APP_STEP_2, 3000);
        }
        else if (msg.action == DelayedActionMsg::Action::APP_STEP_2) {
            qb::io::cout() << "Application waiting for timers to complete...\n";
            
            // Schedule the final shutdown with a delay
            push<DelayedActionMsg>(id(), DelayedActionMsg::Action::APP_STEP_3, 3000);
        }
        else if (msg.action == DelayedActionMsg::Action::APP_STEP_3) {
            qb::io::cout() << "Application shutting down all actors...\n";
            
            // Broadcast kill event to all actors
            broadcast<qb::KillEvent>();
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "Application: Shutting down\n";
        kill();
    }

private:
    qb::ActorId _timer_manager_id;
    int _step;
};

int main() {
    // Create the main engine
    qb::Main engine;
    
    // Create actors
    auto timer_manager_id = engine.addActor<TimerManager>(0);
    engine.addActor<Application>(0, timer_manager_id);
    
    qb::io::cout() << "Main: Starting QB engine\n";
    engine.start();
    
    qb::io::cout() << "Main: Waiting for actors to complete\n";
    engine.join();
    
    qb::io::cout() << "Example completed.\n";
    return 0;
} 