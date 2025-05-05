/**
 * @example Simple Actor
 * 
 * This is a very basic example of the QB Actor Framework.
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
        
        std::cout << "SimpleActor: Initialized" << std::endl;
        return true;
    }
    
    void on(SimpleEvent const& event) {
        std::cout << "SimpleActor: Received SimpleEvent with value " << event.value << std::endl;
        
        // After receiving 5 events, terminate the actor
        if (event.value >= 5) {
            std::cout << "SimpleActor: Terminating" << std::endl;
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
        
        std::cout << "SenderActor: Initialized" << std::endl;
        return true;
    }
    
    void onCallback() override {
        // Send a SimpleEvent to the target
        std::cout << "SenderActor: Sending SimpleEvent with value " << _count << std::endl;
        push<SimpleEvent>(_target_id, _count++);
        
        // After sending 6 events, terminate the actor
        if (_count > 6) {
            std::cout << "SenderActor: Terminating" << std::endl;
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
    std::cout << "Main: Starting QB engine" << std::endl;
    engine.start();
    
    // Wait for all actors to complete
    engine.join();
    
    std::cout << "Main: All actors have terminated, exiting" << std::endl;
    return 0;
} 