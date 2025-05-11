/**
 * @file examples/core/example1_simple_actor.cpp
 * @example Simple Actor Communication and Lifecycle
 *
 * @brief This example demonstrates the fundamental concepts of the QB Actor Framework,
 * including actor creation, basic message passing, event handling, and lifecycle management.
 *
 * @details
 * The example consists of two actors:
 * 1.  `SimpleActor`:
 *     -   Receives `SimpleEvent` messages.
 *     -   Processes these events and prints their content.
 *     -   Terminates itself after receiving a specific number of events.
 * 2.  `SenderActor`:
 *     -   Uses the `qb::ICallback` interface to perform periodic actions.
 *     -   Periodically sends `SimpleEvent` messages to the `SimpleActor`.
 *     -   Terminates itself after sending a specific number of events.
 *
 * The `qb::Main` engine is used to initialize, run, and manage these actors.
 *
 * QB Features Demonstrated:
 * - Actor Creation: `qb::Actor`, `engine.addActor<ActorType>(core_id, args...)`.
 * - Actor Initialization: `virtual bool onInit()`.
 * - Event Definition: Custom event `SimpleEvent` inheriting from `qb::Event`.
 * - Event Registration: `registerEvent<EventType>(*this)` within `onInit()`.
 * - Event Handling: `void on(const EventType& event)`.
 * - Message Sending: `push<EventType>(destination_actor_id, args...)`.
 * - Actor Lifecycle: `kill()` for self-termination.
 * - Periodic Callbacks: `qb::ICallback`, `registerCallback(*this)`, `void onCallback()`.
 * - Engine Management: `qb::Main`, `engine.start()`, `engine.join()`.
 * - Thread-Safe Output: `qb::io::cout()`.
 * - Actor Identification: `id()` to get the `qb::ActorId`.
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <iostream>

// Define a simple event
struct SimpleEvent : public qb::Event {
    int value;
    explicit SimpleEvent(int val) : value(val) {}
};

// Define a simple actor
class SimpleActor : public qb::Actor {
public:
    bool onInit() override {
        // Register the event handler
        registerEvent<SimpleEvent>(*this);
        
        qb::io::cout() << "SimpleActor: Initialized" << std::endl;
        return true;
    }
    
    void on(SimpleEvent const& event) {
        qb::io::cout() << "SimpleActor: Received SimpleEvent with value " << event.value << std::endl;
        
        // After receiving 5 events, terminate the actor
        if (event.value >= 5) {
            qb::io::cout() << "SimpleActor: Terminating" << std::endl;
            kill();
        }
    }
};

// Define a sender actor
class SenderActor : public qb::Actor, public qb::ICallback {
private:
    qb::ActorId _target_id;
    int _count = 1;
    
public:
    explicit SenderActor(qb::ActorId target_id) : _target_id(target_id) {}
    
    bool onInit() override {
        // Register the callback
        registerCallback(*this);
        
        qb::io::cout() << "SenderActor: Initialized" << std::endl;
        return true;
    }
    
    void onCallback() override {
        // Send a SimpleEvent to the target
        qb::io::cout() << "SenderActor: Sending SimpleEvent with value " << _count << std::endl;
        push<SimpleEvent>(_target_id, _count++);
        
        // After sending 6 events, terminate the actor
        if (_count > 6) {
            qb::io::cout() << "SenderActor: Terminating" << std::endl;
            kill();
        }
    }
};

int main() {
    // Create the main engine
    qb::Main engine;
    
    // Create the simple actor
    auto simple_id = engine.addActor<SimpleActor>(0);
    
    // Create the sender actor
    engine.addActor<SenderActor>(0, simple_id);
    
    // Start the engine
    qb::io::cout() << "Main: Starting QB engine" << std::endl;
    engine.start();
    
    // Wait for all actors to complete
    engine.join();
    
    qb::io::cout() << "Main: All actors have terminated, exiting" << std::endl;
    return 0;
} 