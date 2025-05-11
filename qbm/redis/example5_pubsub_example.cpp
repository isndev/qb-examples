/**
 * @file examples/qbm/redis/example5_pubsub_example.cpp
 * @example qbm-redis: Publish/Subscribe Messaging with Actors
 *
 * @brief This example demonstrates Redis Publish/Subscribe (Pub/Sub) functionality
 * integrated into a QB actor system. It creates publisher and subscriber actors
 * that communicate through Redis channels, simulating a basic real-time messaging system.
 *
 * @details
 * The system is composed of several actors:
 * 1.  `PublisherActor`:
 *     -   Connects to Redis using a standard `qb::redis::tcp::client`.
 *     -   Upon receiving a `PublishMessageEvent` (typically from the `CoordinatorActor`),
 *         it uses the `_redis.publish(channel, message)` command to send a message
 *         to the specified Redis channel.
 *     -   Tracks the number of messages published and can notify its coordinator or
 *         initiate its own shutdown after reaching a target.
 * 2.  `SubscriberActor` (multiple instances possible):
 *     -   Uses the specialized `qb::redis::tcp::cb_consumer` client for Pub/Sub.
 *         This client connects to Redis and manages subscription states.
 *     -   A C++ callback function is provided to the `cb_consumer`'s constructor. This
 *         callback is invoked by the consumer whenever a message is received on any
 *         of the channels it's subscribed to.
 *     -   In this example, the callback logs the received message and forwards its content
 *         as a `ReceivedMessageEvent` to the `CoordinatorActor`.
 *     -   Receives `SubscribeEvent` to subscribe to new Redis channels using `_consumer.subscribe(channel)`.
 *     -   Handles `ShutdownEvent` by unsubscribing from all its channels via `_consumer.unsubscribe(channel)`.
 * 3.  `CoordinatorActor`:
 *     -   Manages the lifecycle of the publisher and subscriber actors (created via `addRefActor`).
 *     -   Uses `qb::ICallback` (`onCallback()`) to periodically instruct the `PublisherActor`
 *         (by sending `PublishMessageEvent`) to publish messages to various predefined channels.
 *     -   Initiates subscriptions for `SubscriberActor`s by sending them `SubscribeEvent`s.
 *     -   Listens for `SubscriptionCompleteEvent` from subscribers and `ReceivedMessageEvent`s (for logging).
 *     -   Orchestrates the graceful shutdown of the system by sending `ShutdownEvent`s to
 *         other actors and eventually calling `qb::Main::stop()`.
 *
 * This example showcases how to build event-driven, real-time communication systems
 * leveraging Redis Pub/Sub within the QB actor model.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::Actor`, `qb::Main`, `qb::Event`, `qb::ICallback`.
 * - `qb::redis::tcp::client` for general commands like `publish()`.
 * - `qb::redis::tcp::cb_consumer`: Specialized client for managing Pub/Sub subscriptions and
 *   receiving messages via a C++ callback.
 *   - `cb_consumer.connect()`
 *   - `cb_consumer.subscribe(channel)`
 *   - `cb_consumer.unsubscribe(channel)`
 *   - `cb_consumer.on_disconnected()` for handling Redis disconnections.
 * - Inter-actor communication for coordination and message forwarding.
 * - `qb::io::async::init()`, `qb::io::async::callback()`.
 * - `qb::io::cout()`.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// Event to signal an actor to publish a message
struct PublishMessageEvent : qb::Event {
    std::string channel;
    std::string message;
    
    PublishMessageEvent(std::string ch, std::string msg)
        : channel(std::move(ch)), message(std::move(msg)) {}
};

// Event to signal an actor to subscribe to a channel
struct SubscribeEvent : qb::Event {
    std::string channel;
    
    explicit SubscribeEvent(std::string ch)
        : channel(std::move(ch)) {}
};

// Event to notify about received messages
struct ReceivedMessageEvent : qb::Event {
    std::string channel;
    std::string message;
    
    ReceivedMessageEvent(std::string ch, std::string msg)
        : channel(std::move(ch)), message(std::move(msg)) {}
};

// Event to signal shutdown
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

// Event to notify that subscriptions are complete
struct SubscriptionCompleteEvent : qb::Event {
    explicit SubscriptionCompleteEvent() {}
};

// Publisher actor that publishes messages to Redis channels
class PublisherActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    bool _connected = false;
    qb::ActorId _coordinator_id;
    int _messages_published = 0;
    int _target_messages = 0;

public:
    PublisherActor(qb::ActorId coordinator, int target_messages = 5)
        : _coordinator_id(coordinator), _target_messages(target_messages) {}

    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "PublisherActor initialized" << std::endl;
        
        // Register for events
        registerEvent<PublishMessageEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Connect to Redis
        cout << "Publisher connecting to Redis..." << std::endl;
        
        if (!_redis.connect()) {
            qb::io::cerr() << "Publisher failed to connect to Redis" << std::endl;
            return false;
        }
        
        _connected = true;
        cout << "Publisher connected to Redis successfully!" << std::endl;
        
        return true;
    }

    void on(const PublishMessageEvent& event) {
        auto cout = qb::io::cout();
        
        if (!_connected) {
            qb::io::cerr() << "Cannot publish: not connected to Redis" << std::endl;
            return;
        }
        
        // Publish message to Redis channel
        cout << "Publishing to channel '" << event.channel 
             << "': " << event.message << std::endl;
        
        auto count = _redis.publish(event.channel, event.message);
        cout << "Message delivered to " << count << " subscribers" << std::endl;
        
        _messages_published++;
        
        // If we've reached our publishing target, notify coordinator
        if (_messages_published >= _target_messages) {
            cout << "Published " << _messages_published << " messages, target reached" << std::endl;
            qb::io::async::callback([this]() {
                push<ShutdownEvent>(id());
            }, 1.0); // Wait for messages to be processed
        }
    }

    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << "PublisherActor shutting down after publishing " 
             << _messages_published << " messages" << std::endl;
        
        // Notify coordinator to shutdown the system
        push<ShutdownEvent>(_coordinator_id);
        
        kill();
    }
};

// Subscriber actor that listens for messages on Redis channels
class SubscriberActor : public qb::Actor {
private:
    qb::redis::tcp::cb_consumer _consumer{REDIS_URI, [this](auto&& msg) {
        // This callback is called when a message is received
        auto cout = qb::io::cout();
        cout << "Subscriber received on '" << msg.channel 
             << "': " << msg.message << std::endl;
        
        // Forward message to coordinator
        if (_coordinator_id != qb::ActorId()) {
            push<ReceivedMessageEvent>(_coordinator_id, std::string(msg.channel), std::string(msg.message));
        }
    }};
    
    bool _connected = false;
    std::vector<std::string> _subscribed_channels;
    qb::ActorId _coordinator_id;
    std::string _name;

public:
    SubscriberActor(qb::ActorId coordinator, std::string name = "Subscriber")
        : _coordinator_id(coordinator), _name(std::move(name)) {}

    bool onInit() override {
        auto cout = qb::io::cout();
        cout << _name << " initialized" << std::endl;
        
        // Register for events
        registerEvent<SubscribeEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Setup disconnection handler
        _consumer.on_disconnected([this](qb::io::async::event::disconnected &&e) {
            auto cout = qb::io::cout();
            cout << _name << " disconnected from Redis (reason: " << e.reason << ")" << std::endl;
            _connected = false;
        });
        
        // Connect to Redis
        cout << _name << " connecting to Redis..." << std::endl;
        
        if (!_consumer.connect()) {
            qb::io::cerr() << _name << " failed to connect to Redis" << std::endl;
            return false;
        }
        
        _connected = true;
        cout << _name << " connected to Redis successfully!" << std::endl;
        
        return true;
    }

    void on(const SubscribeEvent& event) {
        auto cout = qb::io::cout();
        
        if (!_connected) {
            qb::io::cerr() << "Cannot subscribe: not connected to Redis" << std::endl;
            return;
        }
        
        // Subscribe to Redis channel
        cout << _name << " subscribing to channel: " << event.channel << std::endl;
        
        auto result = _consumer.subscribe(event.channel);
        if (result.channel.has_value()) {
            cout << _name << " subscribed to channel: " << *result.channel << std::endl;
            _subscribed_channels.push_back(event.channel);
            
            // Notify coordinator that subscription is complete
            push<SubscriptionCompleteEvent>(_coordinator_id);
        } else {
            qb::io::cerr() << _name << " failed to subscribe to channel: " << event.channel << std::endl;
        }
    }

    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << _name << " shutting down, unsubscribing from " 
             << _subscribed_channels.size() << " channels" << std::endl;
        
        // Unsubscribe from all channels
        for (const auto& channel : _subscribed_channels) {
            _consumer.unsubscribe(channel);
            cout << _name << " unsubscribed from: " << channel << std::endl;
        }
        
        kill();
    }
};

// Coordinator actor that manages publishers and subscribers
class CoordinatorActor : public qb::Actor, public qb::ICallback {
private:
    qb::ActorId _publisher_id;
    qb::ActorId _subscriber1_id;
    qb::ActorId _subscriber2_id;
    
    int _message_count = 0;
    int _max_messages = 10;
    bool _shutdown_requested = false;
    int _subscriptions_complete = 0;
    int _expected_subscriptions = 2; // We expect 2 subscribers
    
    std::vector<std::string> _channels = {"news", "sports", "technology"};
    std::vector<std::string> _example_messages = {
        "Breaking News: Important announcement!",
        "Sports Update: Team wins championship!",
        "Technology News: New device released!",
        "Weather Alert: Sunny day ahead!",
        "Traffic Update: Clear roads everywhere!"
    };
    
    PublisherActor* _publisher_ptr = nullptr;
    SubscriberActor* _subscriber1_ptr = nullptr;
    SubscriberActor* _subscriber2_ptr = nullptr;

public:
    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor initialized" << std::endl;
        
        // Register for events
        registerEvent<ReceivedMessageEvent>(*this);
        registerEvent<SubscriptionCompleteEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        
        // Register for callbacks to trigger periodic message publishing
        registerCallback(*this);
        
        initialize_system();
        
        return true;
    }

    void initialize_system() {
        auto cout = qb::io::cout();
        
        // Create publisher
        _publisher_ptr = addRefActor<PublisherActor>(id(), _max_messages);
        if (!_publisher_ptr) {
            qb::io::cerr() << "Failed to create publisher actor" << std::endl;
            kill();
            return;
        }
        _publisher_id = _publisher_ptr->id();
        
        // Create subscribers with different names
        _subscriber1_ptr = addRefActor<SubscriberActor>(id(), "Subscriber1");
        if (!_subscriber1_ptr) {
            qb::io::cerr() << "Failed to create subscriber1 actor" << std::endl;
            kill();
            return;
        }
        _subscriber1_id = _subscriber1_ptr->id();
        
        _subscriber2_ptr = addRefActor<SubscriberActor>(id(), "Subscriber2");
        if (!_subscriber2_ptr) {
            qb::io::cerr() << "Failed to create subscriber2 actor" << std::endl;
            kill();
            return;
        }
        _subscriber2_id = _subscriber2_ptr->id();
        
        cout << "Created Publisher: " << _publisher_id << std::endl;
        cout << "Created Subscriber1: " << _subscriber1_id << std::endl;
        cout << "Created Subscriber2: " << _subscriber2_id << std::endl;
        
        // Subscribe to channels with delay to allow connections to establish
        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "Setting up subscriptions..." << std::endl;
            
            // First subscriber subscribes to first two channels
            push<SubscribeEvent>(_subscriber1_id, _channels[0]);
            push<SubscribeEvent>(_subscriber1_id, _channels[1]);
            
            // Second subscriber subscribes to last two channels
            // This creates overlap on the middle channel
            push<SubscribeEvent>(_subscriber2_id, _channels[1]);
            push<SubscribeEvent>(_subscriber2_id, _channels[2]);
        }, 1.0); // Wait 1 second for connections to be ready
    }

    // Periodic callback to publish messages
    void onCallback() override {
        // Don't start publishing until subscribers are ready
        if (_subscriptions_complete < _expected_subscriptions || _shutdown_requested) {
            return;
        }
        
        // Publish a message to a random channel
        if (_message_count < _max_messages) {
            int channel_idx = _message_count % _channels.size();
            int message_idx = _message_count % _example_messages.size();
            
            push<PublishMessageEvent>(_publisher_id, 
                                     _channels[channel_idx], 
                                     _example_messages[message_idx]);
            
            _message_count++;
        }
    }

    void on(const ReceivedMessageEvent& event) {
        // Just log that we received a forwarded message
        // (Real application would process this message)
        auto cout = qb::io::cout();
        cout << "Coordinator received forwarded message from channel '" 
             << event.channel << "': " << event.message << std::endl;
    }
    
    void on(const SubscriptionCompleteEvent&) {
        auto cout = qb::io::cout();
        _subscriptions_complete++;
        cout << "Subscription complete notification received. " 
             << _subscriptions_complete << " of " 
             << _expected_subscriptions << " complete." << std::endl;
        
        if (_subscriptions_complete >= _expected_subscriptions) {
            cout << "All subscriptions complete. Starting to publish messages..." << std::endl;
        }
    }

    void on(const qb::KillEvent&) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received kill event" << std::endl;
        
        shutdown_system();
    }
    
    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received shutdown event" << std::endl;
        
        shutdown_system();
    }
    
    void shutdown_system() {
        auto cout = qb::io::cout();
        if (_shutdown_requested) return;
        
        _shutdown_requested = true;
        cout << "Shutting down pub/sub system..." << std::endl;
        
        // Send shutdown events to all actors
        if (_publisher_ptr) {
            push<ShutdownEvent>(_publisher_id);
        }
        
        if (_subscriber1_ptr) {
            push<ShutdownEvent>(_subscriber1_id);
        }
        
        if (_subscriber2_ptr) {
            push<ShutdownEvent>(_subscriber2_id);
        }
        
        // Wait a moment before killing coordinator
        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "CoordinatorActor shutting down" << std::endl;
            kill();
            
            // Wait a moment then stop the engine
            qb::io::async::callback([]() {
                auto cout = qb::io::cout();
                cout << "Stopping engine..." << std::endl;
                qb::Main::stop();
            }, 1.0);
        }, 2.0);
    }
};

int main() {
    // Initialize the async system
    qb::io::async::init();
    auto cout = qb::io::cout();
    
    cout << "Starting Redis Pub/Sub Example" << std::endl;
    
    // Create the engine
    qb::Main engine;
    
    // Add coordinator actor to core 0
    auto coordinator_id = engine.addActor<CoordinatorActor>(0);
    if (coordinator_id == 0) {  // 0 is an invalid actor ID
        qb::io::cerr() << "Failed to create coordinator actor" << std::endl;
        return 1;
    }
    
    // Start the engine (non-blocking)
    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;
    
    // Wait for engine to stop (when all actors terminate)
    engine.join();
    
    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Pub/Sub Example completed successfully" << std::endl;
    
    return 0;
} 