/**
 * @file examples/01-actors/04-cores-and-placement.cpp
 * @tier 01-actors
 * @teaches Where an actor runs and how you address it once it is there: placing actors on
 *          several cores, reading the core back out of an id, and the system-wide broadcast.
 * @demonstrates qb::Main, addActor<T>, id(), getIndex(), qb::ActorId, registerEvent<E>, push<E>,
 *               broadcast<E>, spawn, qb::ScopedCoroContext, ctx.sleep, kill()
 * @prerequisites 01-actors/02-messaging
 * @expect "DispatcherActor: Broadcasting system notification"
 * @expect "DispatcherActor: All events dispatched, terminating"
 * @expect "All work completed, processed "
 *
 * @example Multi-Core Actor Distribution and Event Broadcasting
 *
 * @brief This example illustrates how to distribute actors across multiple CPU cores
 * and how to use broadcast events for system-wide or core-specific notifications.
 * It simulates a workload distributed by a dispatcher to worker actors running on different cores.
 *
 * @details
 * The system comprises:
 * 1.  `WorkerActor`:
 *     -   Deployed on multiple CPU cores (configurable, up to `std::thread::hardware_concurrency()`).
 *     -   Handles three types of events: `HighPriorityEvent`, `StandardEvent`, and `LowPriorityEvent`.
 *     -   Simulates different processing times based on event priority, without blocking its core.
 *     -   Receives `SystemNotificationEvent`s broadcast by the dispatcher.
 *     -   Terminates after processing a predefined number of events.
 * 2.  `DispatcherActor`:
 *     -   Runs on a specific core (core 0 in this example).
 *     -   Dispatches work events to `WorkerActor`s in a round-robin fashion, paced by a
 *         coroutine timer.
 *     -   Periodically sends `SystemNotificationEvent`s to every actor in the system with
 *         `broadcast<SystemNotificationEvent>(...)`.
 *     -   Terminates after dispatching all planned work.
 *
 * The `qb::Main` engine manages the actors and their assignment to cores.
 *
 * @note `qb::BroadcastId(core_id)` IS NOT A SYSTEM-WIDE BROADCAST. It addresses every actor on
 *       ONE core (`qb/src/qb/core/ActorId.h`: "used to send messages to all actors on a specific
 *       core"). This file used to send its notifications to `qb::BroadcastId(0)` while three of
 *       its four workers ran on cores 1..3, so those three reported `Notifications: 0` on every
 *       run -- and the note that used to sit here named `broadcast<T>()` as the fix without
 *       applying it. `broadcast<T>(args...)` fans out to every core; keep `BroadcastId` for when
 *       you deliberately mean one core's actors and nobody else's.
 *
 * @note NEVER BLOCK A HANDLER. Each worker used to `std::this_thread::sleep_for` its "processing
 *       time" and the dispatcher used to sleep 10ms per dispatch. A sleep in a handler stops the
 *       whole VirtualCore -- every actor on it, its timers, and its I/O -- so on this layout the
 *       dispatcher's sleep also froze the worker sharing core 0. Use `spawn(...)` +
 *       `co_await ctx.sleep(d)` and hand the result back with `ctx.push<T>(...)`.
 *
 * QB Features Demonstrated:
 * - Multi-Core Actor Assignment: `engine.addActor<ActorType>(core_id, args...)`.
 * - Actor Concurrency: Multiple `WorkerActor`s processing events in parallel on different cores.
 * - Event System: Multiple custom event types (`HighPriorityEvent`, `StandardEvent`, `LowPriorityEvent`, `SystemNotificationEvent`).
 * - Event Handling: `onInit()`, `registerEvent<EventType>()`, `on(EventType& event)`.
 * - Message Sending: `push<EventType>(destination_actor_id, args...)`.
 * - Broadcast Messaging: `broadcast<EventType>(args...)` system-wide, versus
 *   `push<EventType>(qb::BroadcastId(core_id), args...)` for one core's actors.
 * - Non-Blocking Delays: `spawn(...)` + `co_await ctx.sleep(...)`.
 * - Actor Lifecycle: `kill()` for self-termination.
 * - Core Information: `getIndex()` to retrieve the actor's current core ID.
 * - Engine Management: `qb::Main`, `std::thread::hardware_concurrency()`.
 * - Thread-Safe I/O: `qb::io::cout()`.
 */

#include <chrono>
#include <string_view>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// Define event types with different priorities
struct HighPriorityEvent : public qb::Event {
    int value;
    explicit HighPriorityEvent(int val)
        : value(val) {}
};

struct StandardEvent : public qb::Event {
    int value;
    explicit StandardEvent(int val)
        : value(val) {}
};

struct LowPriorityEvent : public qb::Event {
    int value;
    explicit LowPriorityEvent(int val)
        : value(val) {}
};

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
//
// Broadcast event for system-wide notifications
struct SystemNotificationEvent : public qb::Event {
    qb::string<96> message;
    explicit SystemNotificationEvent(std::string_view msg)
        : message(msg) {}
};

// Emitted by a worker's own coroutine once the simulated processing time for one item has
// elapsed. The coroutine may not touch actor state, so it carries the only thing it needs to
// say -- which kind of work finished -- and the handler does the counting in actor context.
struct WorkFinishedEvent : public qb::Event {
    enum class Kind { STANDARD, HIGH, LOW };

    Kind kind;
    int  value;

    WorkFinishedEvent(Kind k, int v)
        : kind(k)
        , value(v) {}
};

// Worker actor that processes events of different priorities
class WorkerActor : public qb::Actor {
private:
    int       _processed_standard     = 0;
    int       _processed_high         = 0;
    int       _processed_low          = 0;
    int       _notifications_received = 0;
    const int _max_events             = 5;
    std::string
    _timestamp() const {
        auto now    = std::chrono::system_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto value  = now_ms.time_since_epoch().count();
        return "[" + std::to_string(value % 10000) + "] ";
    }

public:
    WorkerActor() {
        // Register for different event types
        registerEvent<StandardEvent>(*this);
        registerEvent<HighPriorityEvent>(*this);
        registerEvent<LowPriorityEvent>(*this);
        registerEvent<SystemNotificationEvent>(*this);
        registerEvent<WorkFinishedEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Initialized on core " << getIndex() << std::endl;
        co_return true;
    }

    // Handlers for different event types
    void
    on(StandardEvent &event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing StandardEvent with value " << event.value << std::endl;
        // Simulate work for standard priority
        startWork(WorkFinishedEvent::Kind::STANDARD, event.value, 50ms);
    }

    void
    on(HighPriorityEvent &event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing HighPriorityEvent with value " << event.value << std::endl;
        // High priority tasks are processed faster
        startWork(WorkFinishedEvent::Kind::HIGH, event.value, 20ms);
    }

    void
    on(LowPriorityEvent &event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processing LowPriorityEvent with value " << event.value << std::endl;
        // Low priority tasks take longer
        startWork(WorkFinishedEvent::Kind::LOW, event.value, 80ms);
    }

    void
    on(WorkFinishedEvent &event) {
        const char *label = nullptr;
        int         total = 0;

        switch (event.kind) {
            case WorkFinishedEvent::Kind::STANDARD:
                label = "StandardEvent";
                total = ++_processed_standard;
                break;
            case WorkFinishedEvent::Kind::HIGH:
                label = "HighPriorityEvent";
                total = ++_processed_high;
                break;
            case WorkFinishedEvent::Kind::LOW:
                label = "LowPriorityEvent";
                total = ++_processed_low;
                break;
        }

        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Completed " << label << ", total: " << total << std::endl;

        checkCompletion();
    }

    void
    on(SystemNotificationEvent &event) {
        qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Received notification: " << event.message << std::endl;
        _notifications_received++;
    }

private:
    // Suspend for the simulated processing time instead of blocking the core, then report back
    // into actor context. Because the wait does not block, a worker can have several items in
    // flight at once and its core keeps delivering notifications throughout -- which is what
    // makes the `Notifications:` line at the end non-zero on every worker.
    void
    startWork(WorkFinishedEvent::Kind kind, int value, qb::duration cost) {
        spawn([kind, value, cost](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(cost);
            ctx.template push<WorkFinishedEvent>(kind, value);
        });
    }

    void
    checkCompletion() {
        // Check if we've processed enough events of each type
        if (_processed_standard + _processed_high + _processed_low >= _max_events * 3) {
            qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Processed enough events, terminating" << std::endl;
            qb::io::cout() << _timestamp() << "WorkerActor " << id() << ": Standard: " << _processed_standard << ", High: " << _processed_high
                           << ", Low: " << _processed_low << ", Notifications: " << _notifications_received << std::endl;
            kill();
        }
    }
};

// Wakes the dispatcher when it is time to send the next work item.
struct DispatchTickEvent : public qb::Event {};

// Dispatcher actor that distributes work across cores
class DispatcherActor : public qb::Actor {
private:
    std::vector<qb::ActorId> _workers;
    int                      _dispatched_events     = 0;
    const int                _max_events_per_worker = 5;
    const int                _num_workers;

public:
    explicit DispatcherActor(const std::vector<qb::ActorId> &workers)
        : _workers(workers)
        , _num_workers(workers.size()) {
        registerEvent<DispatchTickEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "DispatcherActor " << id() << ": Initialized on core " << getIndex() << ", will dispatch to " << _num_workers
                       << " workers" << std::endl;

        dispatchOne();
        co_return true;
    }

    void
    on(DispatchTickEvent const &) {
        dispatchOne();
    }

private:
    void
    dispatchOne() {
        if (_dispatched_events < _max_events_per_worker * _num_workers * 3) {
            // Round-robin dispatch
            int worker_index = (_dispatched_events / 3) % _num_workers;
            int event_type   = _dispatched_events % 3;
            int value        = _dispatched_events;

            // Send different event types based on the current count
            switch (event_type) {
                case 0:
                    qb::io::cout() << "DispatcherActor: Sending StandardEvent to worker " << _workers[worker_index] << std::endl;
                    push<StandardEvent>(_workers[worker_index], value);
                    break;
                case 1:
                    qb::io::cout() << "DispatcherActor: Sending HighPriorityEvent to worker " << _workers[worker_index] << std::endl;
                    push<HighPriorityEvent>(_workers[worker_index], value);
                    break;
                case 2:
                    qb::io::cout() << "DispatcherActor: Sending LowPriorityEvent to worker " << _workers[worker_index] << std::endl;
                    push<LowPriorityEvent>(_workers[worker_index], value);
                    break;
            }

            _dispatched_events++;

            // Every 10 events, send a broadcast notification to EVERY core, not just this one.
            if (_dispatched_events % 10 == 0) {
                std::string msg = "Progress update: " + std::to_string(_dispatched_events) + " events dispatched";
                qb::io::cout() << "DispatcherActor: Broadcasting system notification" << std::endl;
                broadcast<SystemNotificationEvent>(msg);
            }

            // Schedule the next dispatch with a small delay -- suspended, not blocked, so the
            // worker that shares this core keeps running while we wait.
            spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                co_await ctx.sleep(10ms);
                ctx.template push<DispatchTickEvent>();
            });
        } else {
            qb::io::cout() << "DispatcherActor: All events dispatched, terminating" << std::endl;
            // Send final notification
            std::string msg = "All work completed, processed " + std::to_string(_dispatched_events) + " events";
            broadcast<SystemNotificationEvent>(msg);
            kill();
        }
    }
};

int
main() {
    // Get the number of hardware cores
    const unsigned int num_cores = std::thread::hardware_concurrency();
    // Use at least 2 cores, but no more than what's available
    const unsigned int cores_to_use = std::max(2u, std::min(4u, num_cores));

    qb::io::cout() << "Main: Using " << cores_to_use << " cores" << std::endl;

    // Create the main engine
    qb::Main engine;

    // Create worker actors on different cores
    std::vector<qb::ActorId> workers;
    for (unsigned int i = 0; i < cores_to_use; ++i) {
        workers.push_back(engine.addActor<WorkerActor>(i % cores_to_use));
    }

    // Create dispatcher actor on core 0
    engine.addActor<DispatcherActor>(0, workers);

    qb::io::cout() << "Main: Starting QB engine" << std::endl;
    engine.start();

    qb::io::cout() << "Main: Waiting for actors to complete" << std::endl;
    engine.join();

    qb::io::cout() << "Main: All actors have terminated, exiting" << std::endl;
    return 0;
}