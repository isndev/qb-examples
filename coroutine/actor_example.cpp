/**
 * @file examples/coroutine/actor_example.cpp
 * @brief Actor with coroutine example
 *
 * This example demonstrates using coroutines within QB Actors.
 * It shows the proper pattern for async I/O while maintaining Actor safety.
 *
 * Build:
 *   cmake --preset dev && cmake --build --preset dev --target actor_example
 *
 * Run:
 *   ./build/presets/dev/bin/actor_example
 *
 * (The previous note here named the target `actor_coroutine_example`, which does not exist --
 * examples/coroutine/CMakeLists.txt declares `actor_example`.)
 */

#include <chrono>
#include <string>
#include <string_view>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qb/string.h>

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
// `qb::ActorId` is a trivially-copyable 32-bit id, so carrying one is always safe.
//
// Events for our example
struct StartProcessing : qb::Event {
    int            request_id;
    qb::string<64> data;
    qb::ActorId    requester;

    StartProcessing(int id, std::string_view d, qb::ActorId who)
        : request_id(id)
        , data(d)
        , requester(who) {}
};

struct ProcessingComplete : qb::Event {
    int            request_id;
    qb::string<64> result;
    uint64_t       processing_time_ns;
    qb::ActorId    requester;

    ProcessingComplete(int id, std::string_view r, uint64_t time, qb::ActorId who)
        : request_id(id)
        , result(r)
        , processing_time_ns(time)
        , requester(who) {}
};

// Sent by the worker to whoever submitted the request, once that request is fully handled.
// This is what makes the shutdown below deterministic instead of timed.
struct RequestDone : qb::Event {
    int request_id;

    explicit RequestDone(int id)
        : request_id(id) {}
};

// Simulated async service (could be Redis, HTTP, database, etc.)
class AsyncService {
public:
    // Simulated async operation.
    //
    // `input` is taken BY VALUE. A coroutine's frame stores its parameters, and a reference
    // parameter stores only the reference -- with `suspend_always` as the initial suspend, the
    // body does not begin until the scheduler resumes it, by which point a caller temporary is
    // gone. It happens to be safe at the one call site below (the argument outlives the await),
    // but a signature a reader copies must be safe on its own terms.
    static qb::io::async::task<std::string>
    process_data(std::string input) {
        // Simulate network/database delay
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));

        // Process the data
        std::string result = "Processed: " + input;
        co_return result;
    }
};

// Actor that uses coroutines for async I/O
class CoroWorker : public qb::Actor {
    int processed_count_ = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "CoroWorker initialized with ID: " << id() << "\n";

        registerEvent<StartProcessing>(*this);
        registerEvent<ProcessingComplete>(*this);

        // Dispatch is by SUBSCRIPTION, not by vtable: `router::subscribe<E>(handler)` stores a
        // trampoline resolved on the handler's STATIC type, and `qb::Actor`'s constructor
        // already subscribed `KillEvent` with `_Handler = qb::Actor`. Declaring
        // `on(qb::KillEvent const &)` in a derived class therefore hides a name that nothing
        // calls -- the base handler keeps running and the actor still dies, so the failure is
        // completely silent. Re-subscribing here overwrites that entry with a trampoline
        // resolved on `CoroWorker`, which is what makes the handler below run. Note that
        // `qb::Actor::on(KillEvent const &)` is NOT virtual, so `override` on it does not
        // compile: re-registering is the mechanism, not a workaround.
        registerEvent<qb::KillEvent>(*this);

        co_return true;
    }

    // Synchronous event handler - this is where we receive requests
    void
    on(StartProcessing &req) {
        auto start_time = time();

        qb::io::cout() << "[Actor " << id() << "] Received request " << req.request_id << " with data: " << req.data << "\n";

        // ⚠️ CRITICAL: Capture by VALUE only!
        int         req_id = req.request_id;
        std::string data   = req.data.c_str();
        uint64_t    start  = start_time;
        qb::ActorId who    = req.requester;

        // Launch async coroutine scoped to this actor's lifetime
        spawn([req_id, data, start, who](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // This runs in isolated context - we can only use ctx interface
            // NO direct access to Actor members here!

            // Perform async I/O
            std::string result = co_await AsyncService::process_data(data);

            uint64_t elapsed = ctx.time() - start;

            // Return result via event (only way to communicate back)
            ctx.template push<ProcessingComplete>(req_id, result, elapsed, who);

            qb::io::cout() << "[Coroutine] Request " << req_id << " completed in " << elapsed / 1'000'000 << "ms\n";
        });

        qb::io::cout() << "[Actor " << id() << "] Spawned coroutine for request " << req.request_id << "\n";
    }

    // Synchronous event handler - this is where we receive async results
    void
    on(ProcessingComplete &ev) {
        // We're back in safe Actor context with exclusive state access
        ++processed_count_;

        qb::io::cout() << "[Actor " << id() << "] Request " << ev.request_id << " completed. Result: " << ev.result << " (took "
                       << ev.processing_time_ns / 1'000'000 << "ms)\n";

        qb::io::cout() << "[Actor " << id() << "] Total processed: " << processed_count_ << "\n";

        // Tell the submitter this request is finished. `reply(ev)` would send THIS event back to
        // its source -- which is this actor itself, because the coroutine pushed it here -- so it
        // is the wrong primitive; the submitter travels in the payload instead.
        push<RequestDone>(ev.requester, ev.request_id);
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "[Actor " << id() << "] Shutting down. Total processed: " << processed_count_ << "\n";
        kill();
    }
};

// Driver actor that sends test requests to CoroWorker
class TestDriver : public qb::Actor {
    static constexpr int REQUEST_COUNT = 3;

    qb::ActorId _worker_id;
    int         _completed = 0;

public:
    explicit TestDriver(qb::ActorId worker_id)
        : _worker_id(worker_id) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<RequestDone>(*this);
        // Same reason as in CoroWorker::onInit: without this line the handler below is dead code.
        registerEvent<qb::KillEvent>(*this);

        qb::io::cout() << "TestDriver initialized\n";
        qb::io::cout() << "\nSending test requests...\n\n";

        for (int i = 1; i <= REQUEST_COUNT; ++i) {
            push<StartProcessing>(_worker_id, i, "TestData-" + std::to_string(i), id());
            // `onInit()` is itself a coroutine, so it can await -- this staggers the submissions
            // so the log interleaves legibly. It is deliberately NOT load-bearing: shutdown
            // below is driven by counting replies, never by a duration.
            co_await context().sleep(std::chrono::milliseconds(20));
        }

        co_return true;
    }

    void
    on(RequestDone &ev) {
        qb::io::cout() << "TestDriver: request " << ev.request_id << " acknowledged (" << (_completed + 1) << "/" << REQUEST_COUNT << ")\n";

        if (++_completed == REQUEST_COUNT) {
            // The previous version broadcast KillEvent after a hard-coded 1s sleep sized by
            // comment to "3 x 100ms plus slack". On a loaded machine or a sanitizer build that
            // lands early and the example silently prints fewer completions, with exit code 0.
            // The count it needed already existed; this uses it.
            qb::io::cout() << "\nAll " << REQUEST_COUNT << " requests complete -- initiating shutdown...\n";
            broadcast<qb::KillEvent>();
        }
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "TestDriver: shutting down\n";
        kill();
    }
};

// Main function for the example
int
main() {
    qb::io::cout() << "=== QB Actor + Coroutine Example ===\n\n";

    // Create the main engine
    qb::Main engine;

    // Add our coroutine worker actor on core 0
    auto worker_id = engine.addActor<CoroWorker>(0);

    // Add the test driver on core 0, passing the worker's id
    engine.addActor<TestDriver>(0, worker_id);

    // Start the engine
    qb::io::cout() << "Starting QB engine...\n\n";

    engine.start();
    engine.join();

    qb::io::cout() << "\n=== Example completed! ===\n";

    return 0;
}
