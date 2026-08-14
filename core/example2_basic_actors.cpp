/**
 * @file examples/core/example2_basic_actors.cpp
 * @example Basic Actor Request-Response Communication
 *
 * @brief This example demonstrates a common actor communication pattern: request-response.
 * It showcases how actors can send messages and receive replies, managing their
 * state and lifecycle accordingly.
 *
 * @details
 * The example features three main actors:
 * 1.  `ReceiverActor`:
 *     -   Listens for `MessageEvent`s.
 *     -   Simulates processing work upon receiving a message, *asynchronously*.
 *     -   Sends a `ResponseEvent` back to the original sender of the `MessageEvent`.
 *     -   Keeps track of processed and in-flight messages and terminates once both are settled.
 * 2.  `SenderActor` (two instances, "Alice" and "Bob"):
 *     -   Paces its `MessageEvent`s with a coroutine timer rather than a per-turn callback.
 *     -   Listens for `ResponseEvent`s from the `ReceiverActor`.
 *     -   Keeps track of sent messages and received responses, terminating after fulfilling its task.
 *
 * The `qb::Main` engine orchestrates these actors. The use of `event.getSource()` is
 * crucial for the `ReceiverActor` to know where to send the response.
 *
 * @note NEVER BLOCK A HANDLER. All three actors here live on core 0, which is one thread running
 *       one event loop. The previous version of this file called
 *       `std::this_thread::sleep_for(100ms)` inside `ReceiverActor::on(MessageEvent&)` to
 *       "simulate some processing time", and `sleep_for(200ms)` inside the sender's per-turn
 *       callback to slow it down. Each of those froze the OTHER two actors for the duration --
 *       no events delivered, no timers fired, no diagnostic -- and the second one made the whole
 *       example's cadence an accident of the freeze. A delay inside an actor is spelled
 *       `spawn(...)` + `co_await ctx.sleep(d)`: the wait is suspended, not blocked, the core
 *       keeps serving every other actor, and the wait is cancelled automatically if the actor is
 *       killed while it is pending.
 *
 * QB Features Demonstrated:
 * - Actor Creation & Management: `qb::Actor`, `engine.addActor<ActorType>()`.
 * - Event System: Custom `MessageEvent` and `ResponseEvent` inheriting from `qb::Event`.
 * - Event Handling: `onInit()`, `registerEvent<EventType>()`, `void on(EventType& event)` (note non-const for `event.getSource()`).
 * - Message Passing:
 *     - Sending requests: `push<MessageEvent>(receiver_id, ...)`.
 *     - Sending responses: `push<ResponseEvent>(event.getSource(), ...)`.
 * - Actor Lifecycle: `kill()` for self-termination based on application logic.
 * - Non-Blocking Delays: `spawn(...)` + `co_await ctx.sleep(...)`, and `ctx.push<T>()` to hand the
 *   result back into actor context.
 * - Engine Control: `qb::Main`, `engine.start()`, `engine.join()`.
 * - Actor Identification: `id()`, `event.getSource()`.
 * - Thread-Safe I/O: `qb::io::cout()`.
 */

#include <chrono>
#include <string_view>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// Define a message event.
//
// `content` is a `qb::string<64>`, NOT a `std::string`. The engine relocates an event with
// `memcpy` and never runs the source destructor, so a payload member may hold no pointer into
// itself. On libstdc++ a SHORT std::string holds exactly that -- `_M_p` addresses its own inline
// buffer -- so after the relocation it still points at the old storage. libc++ recomputes the
// pointer from `this`, which is why the defect is invisible on macOS and corrupts on Linux.
// Relocation is not a cross-core-only event: pipe growth, compaction, `reply()` and `forward()`
// relocate same-core events too.
struct MessageEvent : public qb::Event {
    qb::string<64> content;
    int            sequence_number;

    MessageEvent(std::string_view msg, int seq)
        : content(msg)
        , sequence_number(seq) {}
};

// Define a response event
struct ResponseEvent : public qb::Event {
    qb::string<64> content;
    int            sequence_number;

    ResponseEvent(std::string_view msg, int seq)
        : content(msg)
        , sequence_number(seq) {}
};

// Emitted by the receiver's own work coroutine once the simulated processing has elapsed. The
// coroutine may not touch actor state, so it hands the two things it knows -- which message, and
// who asked -- back through this event, and the handler does the bookkeeping in actor context.
struct WorkFinishedEvent : public qb::Event {
    int         sequence_number;
    qb::ActorId requester;

    WorkFinishedEvent(int seq, qb::ActorId who)
        : sequence_number(seq)
        , requester(who) {}
};

// A receiver actor that processes messages and sends responses
class ReceiverActor : public qb::Actor {
private:
    int       _processed_count = 0;
    int       _pending_count   = 0;
    const int _max_messages    = 10; // Changed to 10 to handle messages from both senders

public:
    ReceiverActor() {
        // Register for the message event
        registerEvent<MessageEvent>(*this);
        registerEvent<WorkFinishedEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "ReceiverActor " << id() << ": Initialized and waiting for messages\n";
        co_return true;
    }

    // Handler for the message event
    void
    on(MessageEvent &event) {
        _pending_count++;

        qb::io::cout() << "ReceiverActor " << id() << ": Received message #" << event.sequence_number << " with content: \"" << event.content
                       << "\"\n";
        qb::io::cout() << "ReceiverActor " << id() << ": Processing message...\n";

        // Copy everything the coroutine needs BEFORE spawning: it must never read an actor
        // member after a `co_await`, because the actor may be gone by then.
        const int         seq = event.sequence_number;
        const qb::ActorId who = event.getSource();

        // Simulate 100ms of processing WITHOUT freezing the core. `ctx.sleep` is
        // cancellation-aware: if this actor is killed while the wait is pending, the coroutine
        // throws `qb::io::async::cancelled_error` and unwinds instead of resuming into a dead
        // actor's memory.
        spawn([seq, who](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(100ms);
            ctx.template push<WorkFinishedEvent>(seq, who);
        });
    }

    // Back in actor context, with exclusive access to this actor's state.
    void
    on(WorkFinishedEvent &event) {
        _processed_count++;
        _pending_count--;

        // Send a response back to the sender
        std::string response = "Processed message #" + std::to_string(event.sequence_number);
        push<ResponseEvent>(event.requester, response, event.sequence_number);

        // If we've processed enough messages and no pending messages, terminate the actor.
        // `_pending_count` is a real in-flight count now: several messages can be mid-processing
        // at once, which is exactly what a non-blocking handler buys.
        if (_processed_count >= _max_messages && _pending_count == 0) {
            qb::io::cout() << "ReceiverActor " << id() << ": Processed " << _processed_count << " messages, terminating\n";
            kill();
        }
    }
};

// Wakes the sender up when it is time to send the next message.
struct SendTickEvent : public qb::Event {};

// A sender actor that sends messages and receives responses
class SenderActor : public qb::Actor {
private:
    qb::ActorId       _receiver_id;
    int               _sent_count         = 0;
    int               _responses_received = 0;
    const int         _max_messages       = 5;
    const std::string _name;

public:
    SenderActor(const std::string &name, qb::ActorId receiver_id)
        : _receiver_id(receiver_id)
        , _name(name) {
        // Register for the response event
        registerEvent<ResponseEvent>(*this);
        registerEvent<SendTickEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Initialized\n";
        // Send the first message straight away, then pace the rest with a coroutine timer.
        // `qb::ICallback` is deliberately NOT used here: it fires on every turn of the core's
        // event loop, which is microseconds apart, so pacing it needs a sleep -- and a sleep in
        // a handler stops the whole core.
        sendNext();
        co_return true;
    }

    void
    on(SendTickEvent const &) {
        sendNext();
    }

    // Handler for the response event
    void
    on(ResponseEvent &event) {
        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Received response for message #" << event.sequence_number << ": \""
                       << event.content << "\"\n";

        _responses_received++;

        // If we've received all the responses, terminate the actor
        if (_responses_received >= _max_messages) {
            qb::io::cout() << "SenderActor " << _name << " " << id() << ": Received all responses, terminating\n";
            kill();
        }
    }

private:
    void
    sendNext() {
        if (_sent_count >= _max_messages)
            return;

        _sent_count++;
        std::string message = "Message from " + _name + " #" + std::to_string(_sent_count);

        qb::io::cout() << "SenderActor " << _name << " " << id() << ": Sending " << message << "\n";
        push<MessageEvent>(_receiver_id, message, _sent_count);

        if (_sent_count < _max_messages) {
            // Wait 200ms before the next one. Nothing else on this core is held up while we do.
            spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                co_await ctx.sleep(200ms);
                ctx.template push<SendTickEvent>();
            });
        }
    }
};

int
main() {
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