/**
 * @file examples/qbm/redis/example5_pubsub_example.cpp
 * @example qbm-redis: Publish/Subscribe Messaging with Actors (Coroutine API)
 *
 * @brief This example demonstrates Redis Publish/Subscribe (Pub/Sub) functionality
 * integrated into a QB actor system using the modern coroutine API.  It creates
 * publisher and subscriber actors that communicate through Redis channels,
 * simulating a basic real-time messaging system.
 *
 * @details
 * The system is composed of several actors:
 * 1.  `PublisherActor`:
 *     -   Connects to Redis using a `qb::redis::tcp::client`.
 *     -   `onInit()` is a `qb::io::async::task<bool>` coroutine.
 *     -   Upon receiving a `PublishMessageEvent`, spawns a coroutine that calls
 *         `co_await _redis.publish(channel, message)` and logs the subscriber count.
 *     -   Self-shuts-down after reaching the message target.
 * 2.  `SubscriberActor` (multiple instances possible):
 *     -   Uses `qb::redis::tcp::co_consumer` — the coroutine-based Pub/Sub consumer.
 *     -   `onInit()` connects the consumer and subscribes to requested channels.
 *     -   A long-lived receive loop coroutine runs via `spawn()`:
 *           `while (auto msg = co_await _consumer.receive()) { ... }`
 *         Each received message is forwarded to the `CoordinatorActor`.
 *     -   Handles `SubscribeEvent` to add more channels at runtime.
 *     -   Handles `ShutdownEvent` by disconnecting the consumer (which closes the
 *         receive channel and ends the loop cleanly) and then killing the actor.
 * 3.  `CoordinatorActor`:
 *     -   Manages the lifecycle of publisher and subscriber actors.
 *     -   Uses `qb::ICallback` (`onCallback()`) to periodically trigger publishing.
 *     -   Initiates subscriptions, listens for forwarded messages, and orchestrates
 *         graceful shutdown.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::io::async::task<bool>` onInit() coroutine.
 * - `qb::redis::tcp::client` for `co_await publish()`.
 * - `qb::redis::tcp::co_consumer`: coroutine-based Pub/Sub consumer with `receive()`.
 *   - `co_await consumer.connect()`
 *   - `co_await consumer.subscribe(channel)` → `Reply<qb::redis::subscription>`
 *   - `co_await consumer.receive()` → `std::optional<qb::redis::message>` loop
 *   - `consumer.disconnect()` to close the channel and end the loop
 * - `spawn()` for actor-scoped background coroutines.
 * - `qb::io::async::callback` with `std::chrono::duration` timeout.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
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

// Event to notify that a subscription is confirmed
struct SubscriptionCompleteEvent : qb::Event {
    explicit SubscriptionCompleteEvent() {}
};

// Publisher actor that publishes messages to Redis channels
class PublisherActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    qb::ActorId _coordinator_id;
    int _messages_published = 0;
    int _target_messages    = 0;

public:
    PublisherActor(qb::ActorId coordinator, int target_messages = 5)
        : _coordinator_id(coordinator), _target_messages(target_messages) {}

    qb::io::async::task<bool> onInit() override {
        auto cout = qb::io::cout();
        cout << "PublisherActor initialized" << std::endl;

        registerEvent<PublishMessageEvent>(*this);
        registerEvent<ShutdownEvent>(*this);

        cout << "Publisher connecting to Redis..." << std::endl;

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "Publisher failed to connect to Redis" << std::endl;
            co_return false;
        }

        cout << "Publisher connected to Redis successfully!" << std::endl;
        co_return true;
    }

    void on(const PublishMessageEvent& event) {
        std::string channel = event.channel;
        std::string message = event.message;

        spawn([this, channel, message](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << "Publishing to channel '" << channel
                 << "': " << message << std::endl;

            auto r = co_await _redis.publish(channel, message);
            if (r.ok()) {
                cout << "Message delivered to " << r.result()
                     << " subscribers" << std::endl;
            }

            _messages_published++;

            if (_messages_published >= _target_messages) {
                cout << "Published " << _messages_published
                     << " messages, target reached" << std::endl;
                qb::io::async::callback([this]() {
                    push<ShutdownEvent>(id());
                }, std::chrono::seconds(1));
            }
        });
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

// Subscriber actor that listens for messages on Redis channels using co_consumer
class SubscriberActor : public qb::Actor {
private:
    qb::redis::tcp::co_consumer _consumer{REDIS_URI};
    std::vector<std::string> _subscribed_channels;
    qb::ActorId _coordinator_id;
    std::string _name;

public:
    SubscriberActor(qb::ActorId coordinator, std::string name = "Subscriber")
        : _coordinator_id(coordinator), _name(std::move(name)) {}

    qb::io::async::task<bool> onInit() override {
        auto cout = qb::io::cout();
        cout << _name << " initialized" << std::endl;

        registerEvent<SubscribeEvent>(*this);
        registerEvent<ShutdownEvent>(*this);

        cout << _name << " connecting to Redis..." << std::endl;

        if (!co_await _consumer.connect()) {
            qb::io::cerr() << _name << " failed to connect to Redis" << std::endl;
            co_return false;
        }

        cout << _name << " connected to Redis successfully!" << std::endl;

        // Spawn the long-lived receive loop
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            while (auto msg = co_await _consumer.receive()) {
                cout << _name << " received on '" << msg->channel
                     << "': " << msg->payload << std::endl;

                if (_coordinator_id != qb::ActorId()) {
                    push<ReceivedMessageEvent>(
                        _coordinator_id,
                        std::string(msg->channel),
                        std::string(msg->payload));
                }
            }
            cout << _name << " receive loop ended" << std::endl;
        });

        co_return true;
    }

    void on(const SubscribeEvent& event) {
        std::string channel = event.channel;

        spawn([this, channel](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << _name << " subscribing to channel: " << channel << std::endl;

            auto r = co_await _consumer.subscribe(channel);
            if (r.ok()) {
                cout << _name << " subscribed to channel: "
                     << channel << std::endl;
                _subscribed_channels.push_back(channel);

                // Notify coordinator that subscription is confirmed
                push<SubscriptionCompleteEvent>(_coordinator_id);
            } else {
                qb::io::cerr() << _name
                               << " failed to subscribe to channel: "
                               << channel << std::endl;
            }
        });
    }

    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << _name << " shutting down, unsubscribing from "
             << _subscribed_channels.size() << " channels" << std::endl;

        // Disconnect the consumer — this closes the internal channel, which makes
        // the receive() loop return nullopt and the spawned coroutine exit cleanly.
        _consumer.disconnect();

        kill();
    }
};

// Coordinator actor that manages publishers and subscribers
class CoordinatorActor : public qb::Actor, public qb::ICallback {
private:
    qb::ActorId _publisher_id;
    qb::ActorId _subscriber1_id;
    qb::ActorId _subscriber2_id;

    int  _message_count          = 0;
    int  _max_messages           = 10;
    bool _shutdown_requested     = false;
    int  _subscriptions_complete = 0;
    int  _expected_subscriptions = 2; // one per subscriber

    std::vector<std::string> _channels = {"news", "sports", "technology"};
    std::vector<std::string> _example_messages = {
        "Breaking News: Important announcement!",
        "Sports Update: Team wins championship!",
        "Technology News: New device released!",
        "Weather Alert: Sunny day ahead!",
        "Traffic Update: Clear roads everywhere!"
    };

public:
    qb::io::async::task<bool> onInit() override {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor initialized" << std::endl;

        // Register for events before the first co_await
        registerEvent<ReceivedMessageEvent>(*this);
        registerEvent<SubscriptionCompleteEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerCallback(*this);

        // Create publisher
        auto pub_h = addRefActor<PublisherActor>(id(), _max_messages);
        if (!pub_h.valid()) {
            qb::io::cerr() << "Failed to create publisher actor" << std::endl;
            co_return false;
        }
        _publisher_id = pub_h.id();

        // Create subscriber 1
        auto sub1_h = addRefActor<SubscriberActor>(id(), "Subscriber1");
        if (!sub1_h.valid()) {
            qb::io::cerr() << "Failed to create subscriber1 actor" << std::endl;
            co_return false;
        }
        _subscriber1_id = sub1_h.id();

        // Create subscriber 2
        auto sub2_h = addRefActor<SubscriberActor>(id(), "Subscriber2");
        if (!sub2_h.valid()) {
            qb::io::cerr() << "Failed to create subscriber2 actor" << std::endl;
            co_return false;
        }
        _subscriber2_id = sub2_h.id();

        cout << "Created Publisher: " << _publisher_id << std::endl;
        cout << "Created Subscriber1: " << _subscriber1_id << std::endl;
        cout << "Created Subscriber2: " << _subscriber2_id << std::endl;

        // Subscribe to channels after a brief delay to let connections establish
        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "Setting up subscriptions..." << std::endl;

            // Subscriber 1: news + sports
            push<SubscribeEvent>(_subscriber1_id, _channels[0]);
            push<SubscribeEvent>(_subscriber1_id, _channels[1]);

            // Subscriber 2: sports + technology (overlap on sports)
            push<SubscribeEvent>(_subscriber2_id, _channels[1]);
            push<SubscribeEvent>(_subscriber2_id, _channels[2]);
        }, std::chrono::seconds(1));

        co_return true;
    }

    // Periodic callback: publish one message per tick once subscriptions are ready
    void on(qb::LoopEvent const&) override {
        if (_subscriptions_complete < _expected_subscriptions || _shutdown_requested)
            return;

        if (_message_count < _max_messages) {
            int channel_idx = _message_count % static_cast<int>(_channels.size());
            int message_idx = _message_count % static_cast<int>(_example_messages.size());

            push<PublishMessageEvent>(_publisher_id,
                                     _channels[channel_idx],
                                     _example_messages[message_idx]);
            _message_count++;
        }
    }

    void on(const ReceivedMessageEvent& event) {
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
            cout << "All subscriptions complete. Starting to publish messages..."
                 << std::endl;
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

        push<ShutdownEvent>(_publisher_id);
        push<ShutdownEvent>(_subscriber1_id);
        push<ShutdownEvent>(_subscriber2_id);

        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "CoordinatorActor shutting down" << std::endl;
            kill();

            qb::io::async::callback([]() {
                auto cout = qb::io::cout();
                cout << "Stopping engine..." << std::endl;
                qb::Main::stop();
            }, std::chrono::seconds(1));
        }, std::chrono::seconds(2));
    }
};

int main() {
    qb::io::async::init();
    auto cout = qb::io::cout();

    cout << "Starting Redis Pub/Sub Example" << std::endl;

    qb::Main engine;

    auto coordinator_id = engine.addActor<CoordinatorActor>(0);
    if (coordinator_id == 0) {
        qb::io::cerr() << "Failed to create coordinator actor" << std::endl;
        return 1;
    }

    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;

    engine.join();

    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Pub/Sub Example completed successfully" << std::endl;

    return 0;
}
