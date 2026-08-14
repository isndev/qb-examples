/**
 * @file examples/core/example6_shared_queue.cpp
 * @example Producer-Consumer Pattern with a Shared Thread-Safe Queue
 *
 * @brief This example demonstrates how QB actors can interact with an externally managed,
 * thread-safe shared queue, and what that costs you compared with message passing.
 *
 * @details
 * The system consists of:
 * 1.  `SharedQueue<WorkItem>`: A custom thread-safe queue (using `std::mutex`) that stores plain
 *     `WorkItem` values. This is not a QB feature but a standard C++ utility used here to
 *     illustrate interaction with shared memory.
 * 2.  `Producer` Actor:
 *     -   Generates `WorkItem`s on a timer, each with a simulated complexity.
 *     -   Pushes these work items into the `SharedQueue`.
 * 3.  `Consumer` Actors (three instances):
 *     -   Pop `WorkItem`s from the `SharedQueue` and "process" them for a time proportional to
 *         their complexity.
 *     -   Respond to `RequestStatsMsg` from the `Supervisor` with their processing count.
 * 4.  `Supervisor` Actor:
 *     -   Once a second, requests statistics from all `Consumer` actors and reports the queue
 *         depth alongside the aggregate count.
 *     -   After a set duration, initiates a system-wide shutdown by broadcasting `qb::KillEvent`.
 *
 * @warning READ THIS BEFORE COPYING ANYTHING HERE. A mutex-protected container shared by five
 *          actors is the OPPOSITE of the model the rest of this directory teaches. Actors do not
 *          share state; they exchange events, and that is what makes them safe to place on
 *          different cores without reasoning about locks. This file exists to show the seam
 *          where an actor system meets code you did not write -- a legacy queue, a third-party
 *          library, a device driver -- not as a pattern to reach for.
 *
 *          What it costs you, all of it visible in the output:
 *          - Back-pressure disappears. The producer cannot tell that the consumers are behind;
 *            the queue simply grows. `push<WorkItemMsg>(consumer_id, ...)` would put the item in
 *            a pipe the framework accounts for.
 *          - The queue is a lock on a hot path, taken by every producer push, every consumer
 *            pop, and every supervisor sample -- from four different actors.
 *          - Nothing in the type system stops you reaching further into shared state later.
 *
 * @note THREE THINGS IN THIS FILE USED TO BE FALSE, and they compounded.
 *       (1) Every "delay" was a plain `push<DelayedActionMsg>(id(), action, delay_ms)`. `push`
 *       has no delay parameter; `delay_ms` was written and never read. The whole 15-second
 *       script therefore ran in 30 MILLISECONDS.
 *       (2) Because of (1) the consumers ran as a hot self-push loop rather than a paced one,
 *       so the first consumer to poll drained everything: two of the three reported
 *       `Shutting down after processing 0 items` on every run.
 *       (3) With nothing ever waiting, the queue was empty at every single sample, so the
 *       supervisor's `Queue size = 0` line -- the one metric the example exists to show -- was
 *       0 on all 16 samples. Delays are now `spawn(...)` + `co_await ctx.sleep(d)`, which is a
 *       real timer and does not block the VirtualCore the way `std::this_thread::sleep_for`
 *       would.
 *
 * QB Features Demonstrated:
 * - Actor Creation and Management: `qb::Actor`, `engine.addActor<ActorType>()`.
 * - Event System: Custom events for stats and control.
 * - Actor Communication: `push<EventType>(...)` for commands and data.
 * - System-Wide Shutdown: `broadcast<qb::KillEvent>()` plus `registerEvent<qb::KillEvent>()`.
 * - Actor Lifecycle: `onInit()`, `kill()`, handling `qb::KillEvent`.
 * - Non-Blocking Periodic Actions: `spawn(...)` + `co_await ctx.sleep(...)`.
 * - Engine Control: `qb::Main`, `engine.start()`, `engine.join()`.
 * - Interaction with External Shared State: see the @warning above.
 */

#include <chrono>
#include <mutex>
#include <queue>
#include <random>
#include <qb/actor.h>
#include <qb/event.h>
#include <qb/io.h>
#include <qb/main.h>

using namespace std::chrono_literals;

// The unit of work. A PLAIN STRUCT, deliberately: the previous version stored a `qb::Event`
// subclass by value in the `std::queue`. A `qb::Event` carries a routing header (type id,
// source, destination, bucket size) that only the engine may fill in, and copying one into a
// user container copies that header along with it. Events belong in pipes; data belongs in
// containers.
struct WorkItem {
    int id{0};
    int complexity{0}; // Simulates the "complexity" of a work item
};

// Thread-safe shared queue for actors
template <typename T>
class SharedQueue {
public:
    void
    push(const T &item) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(item);
    }

    bool
    pop(T &item) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        item = _queue.front();
        _queue.pop();
        return true;
    }

    size_t
    size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

    bool
    empty() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<T>      _queue;
    mutable std::mutex _mutex;
};

// Self-addressed wake-ups, one per actor role.
struct ProduceTickMsg : public qb::Event {};
struct ConsumeTickMsg : public qb::Event {};
struct SampleTickMsg : public qb::Event {};

// Message to request statistics from a consumer
struct RequestStatsMsg : public qb::Event {
    qb::ActorId requester;

    explicit RequestStatsMsg(qb::ActorId req)
        : requester(req) {}
};

// Response message with consumer statistics
struct ReportStatsMsg : public qb::Event {
    int consumer_id;
    int items_processed;

    ReportStatsMsg(int id, int processed)
        : consumer_id(id)
        , items_processed(processed) {}
};

// Producer that generates work items
class Producer : public qb::Actor {
public:
    explicit Producer(std::shared_ptr<SharedQueue<WorkItem>> queue)
        : _shared_queue(std::move(queue))
        , _rng(std::random_device{}()) {
        registerEvent<ProduceTickMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "Producer " << id() << ": Initialized\n";
        produceOne();
        co_return true;
    }

    void
    on(ProduceTickMsg const &) {
        produceOne();
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "Producer: Shutting down after generating " << _next_id << " items" << std::endl;
        kill();
    }

private:
    void
    produceOne() {
        // The RNG is a MEMBER, seeded once. The previous version constructed a
        // `std::random_device` and an `std::mt19937` on every item, inside a hot handler:
        // seeding mt19937 is ~2.5 KB of state per call, and on macOS `random_device` funnels
        // into a process-wide arc4random lock. An actor owns its state, so keep it.
        std::uniform_int_distribution<> complexity_dist(1, 10);

        WorkItem work{_next_id++, complexity_dist(_rng)};

        qb::io::cout() << "Producer: Generated work item " << work.id << " with complexity " << work.complexity << std::endl;

        _shared_queue->push(work);

        // Produce a little faster than the three consumers can drain, so the queue depth the
        // supervisor reports is actually a number and not a constant zero.
        const auto next_delay = std::chrono::milliseconds(100 + work.complexity * 20);
        spawn([next_delay](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(next_delay);
            ctx.template push<ProduceTickMsg>();
        });
    }

    std::shared_ptr<SharedQueue<WorkItem>> _shared_queue;
    std::mt19937                           _rng;
    int                                    _next_id{0};
};

// Consumer that processes work items
class Consumer : public qb::Actor {
public:
    Consumer(std::shared_ptr<SharedQueue<WorkItem>> queue, int id)
        : _shared_queue(std::move(queue))
        , _consumer_id(id) {
        registerEvent<ConsumeTickMsg>(*this);
        registerEvent<RequestStatsMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "Consumer " << _consumer_id << " (" << id() << "): Initialized\n";
        consumeNext();
        co_return true;
    }

    void
    on(ConsumeTickMsg const &) {
        consumeNext();
    }

    void
    on(RequestStatsMsg &msg) {
        // Send current statistics to the requester
        push<ReportStatsMsg>(msg.requester, _consumer_id, _items_processed);
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "Consumer " << _consumer_id << ": Shutting down after processing " << _items_processed << " items" << std::endl;
        kill();
    }

private:
    void
    consumeNext() {
        WorkItem                  work{};
        std::chrono::milliseconds delay;

        if (_shared_queue->pop(work)) {
            qb::io::cout() << "Consumer " << _consumer_id << ": Processing work item " << work.id << " with complexity " << work.complexity
                           << std::endl;

            _items_processed++;

            // Simulate processing time based on complexity.
            delay = std::chrono::milliseconds(work.complexity * 200);
        } else {
            // Queue is empty, check again later. Polling is a consequence of the shared queue:
            // it cannot wake us, so we have to ask. An event pipe would have delivered.
            delay = 100ms;
        }

        spawn([delay](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(delay);
            ctx.template push<ConsumeTickMsg>();
        });
    }

    std::shared_ptr<SharedQueue<WorkItem>> _shared_queue;
    int                                    _consumer_id;
    int                                    _items_processed{0};
};

// Supervisor that monitors the queue and consumers
class Supervisor : public qb::Actor {
public:
    Supervisor(std::shared_ptr<SharedQueue<WorkItem>> queue, std::vector<qb::ActorId> consumers)
        : _shared_queue(std::move(queue))
        , _consumers(std::move(consumers)) {
        registerEvent<SampleTickMsg>(*this);
        registerEvent<ReportStatsMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "Supervisor " << id() << ": Initialized\n";
        scheduleSample();
        co_return true;
    }

    void
    on(SampleTickMsg const &) {
        // Reset counters for a new statistics collection
        _pending_responses = static_cast<int>(_consumers.size());
        _total_processed   = 0;

        // Request statistics from all consumers
        for (const auto &consumer_id : _consumers) {
            push<RequestStatsMsg>(consumer_id, id());
        }

        // After receiving all responses, processing will continue in on(ReportStatsMsg)
    }

    void
    on(ReportStatsMsg &msg) {
        // Accumulate statistics
        _total_processed += msg.items_processed;
        _pending_responses--;

        // If this is the last response, display statistics and decide what to do next
        if (_pending_responses == 0) {
            qb::io::cout() << "Supervisor: Queue size = " << _shared_queue->size() << ", Total processed = " << _total_processed << std::endl;

            _samples_taken++;
            if (_samples_taken >= SAMPLE_COUNT && !_is_shutting_down) {
                qb::io::cout() << "Supervisor: Shutting down all actors..." << std::endl;
                _is_shutting_down = true;

                // Send KillEvent to all actors (including itself)
                broadcast<qb::KillEvent>();
            } else {
                scheduleSample();
            }
        }
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "Supervisor: Shutting down" << std::endl;
        kill();
    }

private:
    static constexpr int SAMPLE_COUNT = 15; // one sample per second

    void
    scheduleSample() {
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(1s);
            ctx.template push<SampleTickMsg>();
        });
    }

    std::shared_ptr<SharedQueue<WorkItem>> _shared_queue;
    std::vector<qb::ActorId>               _consumers;
    bool                                   _is_shutting_down{false};
    int                                    _samples_taken{0};
    int                                    _pending_responses{0};
    int                                    _total_processed{0};
};

int
main() {
    // Create the main engine
    qb::Main engine;

    // Create a shared queue
    auto shared_queue = std::make_shared<SharedQueue<WorkItem>>();

    // Create the producer
    engine.addActor<Producer>(0, shared_queue);

    // Create multiple consumers
    std::vector<qb::ActorId> consumer_ids;
    for (int i = 0; i < 3; ++i) {
        consumer_ids.push_back(engine.addActor<Consumer>(0, shared_queue, i));
    }

    // Create the supervisor
    engine.addActor<Supervisor>(0, shared_queue, consumer_ids);

    qb::io::cout() << "Main: Starting QB engine\n";
    engine.start();

    qb::io::cout() << "Main: Waiting for actors to complete\n";
    engine.join();

    qb::io::cout() << "Example completed." << std::endl;
    return 0;
}
