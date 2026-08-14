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
 * - `spawn()` for actor-scoped background coroutines, and the rule that goes with it: capture
 *   everything the body READS by value before the first `co_await`, and address other actors by
 *   id through the context. A spawned loop can legitimately outlive the actor — see
 *   `SubscriberActor::onInit()`, where closing the channel is itself part of destroying the actor.
 * - `spawn(...)` + `co_await ctx.sleep(d)` + a self-addressed tick event as the way to wait —
 *   and the one place where a bare `qb::io::async::callback(fn, d)` is still right, in
 *   `CoordinatorActor::on(CoordinatorShutdownTick&)`, where the body captures nothing and is
 *   meant to outlive every actor.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <qbm/redis/redis.h>
#include <string>
#include <string_view>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qb/string.h>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
//
// Event to signal an actor to publish a message
struct PublishMessageEvent : qb::Event {
    qb::string<64>               channel;
    std::shared_ptr<std::string> message; // a pub/sub payload has no bound: box it

    PublishMessageEvent(std::string_view ch, std::string msg)
        : channel(ch)
        , message(std::make_shared<std::string>(std::move(msg))) {}
};

// Event to signal an actor to subscribe to a channel
struct SubscribeEvent : qb::Event {
    qb::string<64> channel;

    explicit SubscribeEvent(std::string_view ch)
        : channel(ch) {}
};

// Event to notify about received messages
struct ReceivedMessageEvent : qb::Event {
    qb::string<64>               channel;
    std::shared_ptr<std::string> message; // a pub/sub payload has no bound: box it

    ReceivedMessageEvent(std::string_view ch, std::string msg)
        : channel(ch)
        , message(std::make_shared<std::string>(std::move(msg))) {}
};

// Event to signal shutdown
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

// Event to notify that a subscription is confirmed
struct SubscriptionCompleteEvent : qb::Event {
    explicit SubscriptionCompleteEvent() {}
};

/**
 * @brief Self-addressed wake-ups for `CoordinatorActor`'s two delays.
 *
 * Both replace a `qb::io::async::callback([this]{ ... }, d)`, whose timer is owned by the event
 * loop rather than by the actor and so fires at an actor that may already be gone.
 */
struct SetupSubscriptionsTick : qb::Event {};  ///< 1 s after init: issue the subscriptions
struct CoordinatorShutdownTick : qb::Event {}; ///< 2 s after shutdown starts: leave

// Publisher actor that publishes messages to Redis channels
class PublisherActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    qb::ActorId            _coordinator_id;
    int                    _messages_published = 0;
    int                    _target_messages    = 0;

public:
    PublisherActor(qb::ActorId coordinator, int target_messages = 5)
        : _coordinator_id(coordinator)
        , _target_messages(target_messages) {}

    qb::io::async::task<bool>
    onInit() override {
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

    void
    on(const PublishMessageEvent &event) {
        std::string channel = event.channel.c_str();
        std::string message = event.message ? *event.message : std::string{};

        spawn([this, channel, message](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << "Publishing to channel '" << channel << "': " << message << std::endl;

            auto r = co_await _redis.publish(channel, message);
            if (r.ok()) {
                cout << "Message delivered to " << r.result() << " subscribers" << std::endl;
            }

            _messages_published++; // safe by OWNERSHIP: `_redis` is a member, so a killed actor never resumes here

            if (_messages_published >= _target_messages) {
                cout << "Published " << _messages_published << " messages, target reached" << std::endl;
                // Already inside a coroutine the actor's cancellation scope owns, so the grace
                // period is just another `co_await`: `ctx.sleep` routes that scope's token, and
                // `ctx.push` addresses this actor by id. The
                // `qb::io::async::callback([this]{ push<ShutdownEvent>(id()); }, 1s)` this
                // replaced allocated a loop-owned timer holding a raw `this` that nothing
                // cancelled when the actor died.
                co_await ctx.sleep(std::chrono::seconds(1));
                ctx.template push<ShutdownEvent>();
            }
        });
    }

    void
    on(const ShutdownEvent &) {
        auto cout = qb::io::cout();
        cout << "PublisherActor shutting down after publishing " << _messages_published << " messages" << std::endl;

        // Notify coordinator to shutdown the system
        push<ShutdownEvent>(_coordinator_id);
        kill();
    }
};

// Subscriber actor that listens for messages on Redis channels using co_consumer
class SubscriberActor : public qb::Actor {
private:
    qb::redis::tcp::co_consumer _consumer{REDIS_URI};
    std::vector<std::string>    _subscribed_channels;
    qb::ActorId                 _coordinator_id;
    std::string                 _name;

public:
    SubscriberActor(qb::ActorId coordinator, std::string name = "Subscriber")
        : _coordinator_id(coordinator)
        , _name(std::move(name)) {}

    qb::io::async::task<bool>
    onInit() override {
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

        // Spawn the long-lived receive loop.
        //
        // `name` and `coordinator` are captured BY VALUE, before the first `co_await`, and the
        // coordinator is addressed by id through `ctx`. That is not tidiness — it is what makes
        // this loop correct, and reading `_name` here instead was a real defect: AddressSanitizer
        // reported `heap-use-after-free` on the final line of this lambda, on every run.
        //
        // The mechanism is worth reading carefully, because it is not the one the code used to
        // claim. `_msg_channel.close()` — the thing that ends this loop — is reached from two
        // places: the consumer's `event::disconnected` handler, and `~RedisCoroConsumer`. In THIS
        // program only the destructor path is taken, which was measured rather than assumed: with
        // `kill()` deferred until the loop reported itself finished, the loop stayed parked for
        // the entire three-second shutdown window and ended only when the engine stopped. So the
        // real order is `on(ShutdownEvent)` → `kill()` → the actor is reaped → the consumer's
        // destructor closes the channel → THIS coroutine resumes with `nullopt`, after `_name`
        // has ceased to exist.
        //
        // Resuming there is safe by design: `qb::io::async::channel`'s `recv_awaiter` holds a
        // `_ch_alive` flag and returns `nullopt` without touching the freed channel
        // (`qb/io/async/coroutine/channel.h`). The framework anticipates the parked receiver
        // outliving its channel. What it cannot anticipate is a lambda reading the actor's
        // members afterwards — so it does not.
        spawn([this, name = _name, coordinator = _coordinator_id](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            while (auto msg = co_await _consumer.receive()) {
                cout << name << " received on '" << msg->channel << "': " << msg->payload << std::endl;

                if (coordinator != qb::ActorId()) {
                    ctx.template push_to<ReceivedMessageEvent>(coordinator, std::string(msg->channel), std::string(msg->payload));
                }
            }
            cout << name << " receive loop ended" << std::endl;
        });

        co_return true;
    }

    void
    on(const SubscribeEvent &event) {
        std::string channel = event.channel.c_str();

        // Same rule as the receive loop: everything the coroutine only READS is captured by
        // value before the first `co_await`, and the coordinator is addressed by id.
        //
        // The one thing that cannot be captured is the WRITE — `_subscribed_channels` has to
        // be the actor's own vector, and it is touched after `co_await _consumer.subscribe()`.
        // That is safe for a different reason than the reads: `_consumer` is a member, so
        // `~Actor` destroys it with its pending-reply queue and the reply callback is dropped
        // UNINVOKED — the coroutine never resumes at all, rather than resuming on a dead actor.
        // Note this differs from the receive loop above, which DOES resume after destruction.
        spawn([this, channel, name = _name, coordinator = _coordinator_id](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto cout = qb::io::cout();
            cout << name << " subscribing to channel: " << channel << std::endl;

            auto r = co_await _consumer.subscribe(channel);
            if (r.ok()) {
                cout << name << " subscribed to channel: " << channel << std::endl;
                _subscribed_channels.push_back(channel);

                // Notify coordinator that subscription is confirmed
                ctx.template push_to<SubscriptionCompleteEvent>(coordinator);
            } else {
                qb::io::cerr() << name << " failed to subscribe to channel: " << channel << std::endl;
            }
        });
    }

    void
    on(const ShutdownEvent &) {
        auto cout = qb::io::cout();
        cout << _name << " shutting down, unsubscribing from " << _subscribed_channels.size() << " channels" << std::endl;

        // Ask the consumer to drop the link, then leave. Do NOT read this as "and therefore the
        // receive loop ends here": measured, the spawned loop is still parked when this handler
        // returns, and it is `~RedisCoroConsumer` — running as part of the `kill()` below — that
        // closes the message channel and resumes it. See the comment on the loop in `onInit()`
        // for why resuming on a destroyed actor is survivable, and what the loop must not do.
        _consumer.disconnect();

        kill();
    }
};

// Coordinator actor that manages publishers and subscribers
class CoordinatorActor
    : public qb::Actor
    , public qb::ICallback {
private:
    qb::ActorId _publisher_id;
    qb::ActorId _subscriber1_id;
    qb::ActorId _subscriber2_id;

    int  _message_count          = 0;
    int  _max_messages           = 10;
    bool _shutdown_requested     = false;
    int  _subscriptions_complete = 0;
    int  _expected_subscriptions = 2; // one per subscriber

    std::vector<std::string> _channels         = {"news", "sports", "technology"};
    std::vector<std::string> _example_messages = {
        "Breaking News: Important announcement!", "Sports Update: Team wins championship!", "Technology News: New device released!",
        "Weather Alert: Sunny day ahead!", "Traffic Update: Clear roads everywhere!"
    };

public:
    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor initialized" << std::endl;

        // Register for events before the first co_await
        registerEvent<ReceivedMessageEvent>(*this);
        registerEvent<SubscriptionCompleteEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<SetupSubscriptionsTick>(*this);
        registerEvent<CoordinatorShutdownTick>(*this);
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

        // Subscribe to channels after a brief delay to let connections establish. The wait lives
        // in a coroutine this actor's cancellation scope owns; the work itself happens in
        // `on(SetupSubscriptionsTick&)`, because it reads `_subscriber*_id` and `_channels`.
        scheduleTick<SetupSubscriptionsTick>(std::chrono::seconds(1));

        co_return true;
    }

    // Periodic callback: publish one message per tick once subscriptions are ready
    void
    on(qb::LoopEvent const &) override {
        if (_subscriptions_complete < _expected_subscriptions || _shutdown_requested)
            return;

        if (_message_count < _max_messages) {
            int channel_idx = _message_count % static_cast<int>(_channels.size());
            int message_idx = _message_count % static_cast<int>(_example_messages.size());

            push<PublishMessageEvent>(_publisher_id, _channels[channel_idx], _example_messages[message_idx]);
            _message_count++;
        }
    }

    void
    on(const ReceivedMessageEvent &event) {
        auto cout = qb::io::cout();
        cout << "Coordinator received forwarded message from channel '" << event.channel
             << "': " << (event.message ? *event.message : std::string{}) << std::endl;
    }

    /**
     * @brief Sleep `d`, then wake this actor with a `TickEvent`
     *
     * The safe replacement for `qb::io::async::callback([this]{ ... }, d)`. That overload
     * heap-allocates a `Timeout` owned by the event loop, not by the actor: nothing cancels it
     * when the actor is killed, so it fires against a destroyed object. `spawn()` runs the body
     * in this actor's cancellation scope and `ctx.sleep(d)` routes that scope's token, so
     * killing the actor cancels the sleep. The body touches no actor state — everything that
     * does lives in the `on(TickEvent)` handler, which only runs on a live actor.
     */
    template <typename TickEvent>
    void
    scheduleTick(qb::duration d) {
        spawn([d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(d);
            ctx.template push<TickEvent>();
        });
    }

    void
    on(const SetupSubscriptionsTick &) {
        auto cout = qb::io::cout();
        cout << "Setting up subscriptions..." << std::endl;

        // Subscriber 1: news + sports
        push<SubscribeEvent>(_subscriber1_id, _channels[0]);
        push<SubscribeEvent>(_subscriber1_id, _channels[1]);

        // Subscriber 2: sports + technology (overlap on sports)
        push<SubscribeEvent>(_subscriber2_id, _channels[1]);
        push<SubscribeEvent>(_subscriber2_id, _channels[2]);
    }

    void
    on(const SubscriptionCompleteEvent &) {
        auto cout = qb::io::cout();
        _subscriptions_complete++;
        cout << "Subscription complete notification received. " << _subscriptions_complete << " of " << _expected_subscriptions << " complete."
             << std::endl;

        if (_subscriptions_complete >= _expected_subscriptions) {
            cout << "All subscriptions complete. Starting to publish messages..." << std::endl;
        }
    }

    void
    on(const qb::KillEvent &) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received kill event" << std::endl;
        shutdown_system();
    }

    void
    on(const ShutdownEvent &) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received shutdown event" << std::endl;
        shutdown_system();
    }

    void
    shutdown_system() {
        auto cout = qb::io::cout();
        if (_shutdown_requested)
            return;

        _shutdown_requested = true;
        cout << "Shutting down pub/sub system..." << std::endl;

        push<ShutdownEvent>(_publisher_id);
        push<ShutdownEvent>(_subscriber1_id);
        push<ShutdownEvent>(_subscriber2_id);

        // Give the others two seconds to finish, then leave. `kill()` runs on the actor, so the
        // wait must come back as an event rather than as a timer holding `this`.
        scheduleTick<CoordinatorShutdownTick>(std::chrono::seconds(2));
    }

    void
    on(const CoordinatorShutdownTick &) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor shutting down" << std::endl;
        kill();

        // This one MUST stay a bare `qb::io::async::callback(fn, d)`, and it is the clearest
        // example in this repository of when that overload is the right tool: it captures
        // NOTHING, it stops the engine rather than touching an actor, and it is supposed to
        // outlive every actor including this one. A `spawn(...)` here would be cancelled by the
        // `kill()` on the line above and the engine would never stop.
        qb::io::async::callback(
            []() {
                auto cout = qb::io::cout();
                cout << "Stopping engine..." << std::endl;
                qb::Main::stop();
            },
            std::chrono::seconds(1));
    }
};

int
main() {
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
