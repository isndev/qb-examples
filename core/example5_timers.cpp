#include <iostream>
#include <chrono>
#include <string>
#include <unordered_map>

#include <qb/actor.h>
#include <qb/main.h>
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
        std::cout << "TimerManager " << id() << ": Initialized\n";
        return true;
    }
    
    void on(StartTimerMsg& msg) {
        std::cout << "Starting timer '" << msg.timer_name 
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
            std::cout << "Cancelling timer '" << msg.timer_name << "'\n";
            _timers.erase(it);
        }
    }
    
    void on(qb::KillEvent&) {
        std::cout << "TimerManager: Shutting down\n";
        
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
            std::cout << "Timer '" << info.name 
                      << "' fired, count: " << info.current_count << std::endl;
            
            // Check if we should stop the timer
            if (info.repeat_count > 0 && info.current_count >= info.repeat_count) {
                std::cout << "Timer '" << info.name << "' completed all " 
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
        std::cout << "Application starting...\n";
        
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
        std::cout << "Application received timer event: " << msg.timer_name 
                  << " fired (count: " << msg.count << ")" << std::endl;
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::APP_STEP_1) {
            std::cout << "Application stopping slow_timer...\n";
            push<CancelTimerMsg>(_timer_manager_id, "slow_timer");
            
            // Schedule the next application step with a longer delay
            // to allow fast_timer to complete all repetitions
            push<DelayedActionMsg>(id(), DelayedActionMsg::Action::APP_STEP_2, 3000);
        }
        else if (msg.action == DelayedActionMsg::Action::APP_STEP_2) {
            std::cout << "Application waiting for timers to complete...\n";
            
            // Schedule the final shutdown with a delay
            push<DelayedActionMsg>(id(), DelayedActionMsg::Action::APP_STEP_3, 3000);
        }
        else if (msg.action == DelayedActionMsg::Action::APP_STEP_3) {
            std::cout << "Application shutting down all actors...\n";
            
            // Broadcast kill event to all actors
            broadcast<qb::KillEvent>();
        }
    }
    
    void on(qb::KillEvent&) {
        std::cout << "Application: Shutting down\n";
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
    
    std::cout << "Main: Starting QB engine\n";
    engine.start();
    
    std::cout << "Main: Waiting for actors to complete\n";
    engine.join();
    
    std::cout << "Example completed.\n";
    return 0;
} 