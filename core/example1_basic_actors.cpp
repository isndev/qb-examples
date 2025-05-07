/**
 * @example Basic Actors
 * 
 * This example demonstrates basic actor communication with the latest QB API:
 * - Actor creation and initialization
 * - Message passing between actors
 * - Event handling
 * - Actor lifecycle management
 */
#include <qb/actor.h>
#include <qb/main.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>

// Define a message event
struct MessageEvent : public qb::Event {
    std::string content;
    int sequence_number;
    
    MessageEvent(const std::string& msg, int seq) 
        : content(msg), sequence_number(seq) {}
};

// Define a response event
struct ResponseEvent : public qb::Event {
    std::string content;
    int sequence_number;
    
    ResponseEvent(const std::string& msg, int seq)
        : content(msg), sequence_number(seq) {}
};

// A receiver actor that processes messages and sends responses
class ReceiverActor : public qb::Actor {
private:
    int _processed_count = 0;
    int _pending_count = 0;
    const int _max_messages = 10;  // Changed to 10 to handle messages from both senders

public:
    ReceiverActor() {
        // Register for the message event
        registerEvent<MessageEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "ReceiverActor " << id() << ": Initialized and waiting for messages\n";
        return true;
    }
    
    // Handler for the message event
    void on(MessageEvent& event) {
        _pending_count++;
        
        qb::io::cout() << "ReceiverActor " << id() << ": Received message #" << event.sequence_number
                  << " with content: \"" << event.content << "\"\n";
        qb::io::cout() << "ReceiverActor " << id() << ": Processing message...\n";
        
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        _processed_count++;
        _pending_count--;
        
        // Send a response back to the sender
        std::string response = "Processed message #" + std::to_string(event.sequence_number);
        push<ResponseEvent>(event.getSource(), response, event.sequence_number);
        
        // If we've processed enough messages and no pending messages, terminate the actor
        if (_processed_count >= _max_messages && _pending_count == 0) {
            qb::io::cout() << "ReceiverActor " << id() << ": Processed " << _processed_count
                      << " messages, terminating\n";
            kill();
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "ReceiverActor " << id() << ": Received kill event\n";
        kill();
    }
};

// A sender actor that sends messages and receives responses
class SenderActor : public qb::Actor, public qb::ICallback {
private:
    qb::ActorId _receiver_id;
    int _sent_count = 0;
    int _responses_received = 0;
    const int _max_messages = 5;
    const std::string _name;
    
public:
    SenderActor(const std::string& name, qb::ActorId receiver_id)
        : _receiver_id(receiver_id), _name(name) {
        // Register for the response event
        registerEvent<ResponseEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Initialized\n";
        // Register a callback to start sending messages - will be called for each core loop
        registerCallback(*this);
        return true;
    }
    
    void onCallback() override {
        if (_sent_count < _max_messages) {
            _sent_count++;
            std::string message = "Message from " + _name + " #" + std::to_string(_sent_count);
            
            qb::io::cout() << "SenderActor " << _name << " " << id() << ": Sending " << message << "\n";
            push<MessageEvent>(_receiver_id, message, _sent_count);
            
            // Add a small delay before the next loop iteration to avoid sending messages too quickly
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    
    // Handler for the response event
    void on(ResponseEvent& event) {
        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Received response for message #"
                  << event.sequence_number << ": \"" << event.content << "\"\n";
        
        _responses_received++;
        
        // If we've received all the responses, terminate the actor
        if (_responses_received >= _max_messages) {
            qb::io::cout() << "SenderActor " << _name << " " << id() << ": Received all responses, terminating\n";
            kill();
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Received kill event\n";
        kill();
    }
};

int main() {
    // Create the main engine
    qb::Main engine;
    
    // Create the receiver actor
    auto receiver_id = engine.addActor<ReceiverActor>(0);
    
    // Create multiple sender actors that communicate with the receiver
    engine.addActor<SenderActor>(0, std::string("Alice"), receiver_id);
    engine.addActor<SenderActor>(0, std::string("Bob"), receiver_id);
    
    qb::io::cout() << "Main: Starting QB engine\n";
    engine.start();
    
    qb::io::cout() << "Main: Waiting for actors to complete\n";
    engine.join();
    
    qb::io::cout() << "Main: All actors have terminated, exiting\n";
    return 0;
} 