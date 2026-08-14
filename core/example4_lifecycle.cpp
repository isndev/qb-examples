/**
 * @file examples/core/example4_lifecycle.cpp
 * @example Actor Lifecycle Management and Supervision
 *
 * @brief This example showcases advanced actor lifecycle management, including
 * starting, stopping, monitoring, and coordinated shutdown of actors using a
 * supervisor-worker pattern.
 *
 * @details
 * The system features:
 * 1.  `WorkerActor`:
 *     -   Represents a unit of work that can be started, monitored, and stopped.
 *     -   Waits for a `StartWorkEvent` to begin its periodic processing task (paced by a
 *         coroutine timer, so the core is never blocked).
 *     -   Responds to `StatusRequestEvent` with its current progress (e.g., items processed).
 *     -   Handles `ShutdownRequestEvent` by stopping new work, draining, and acknowledging
 *         with a `ShutdownCompleteEvent` -- it does NOT terminate itself at that point.
 *     -   Terminates on `qb::KillEvent`, which is the system's terminal signal.
 * 2.  `SupervisorActor`:
 *     -   Manages a group of `WorkerActor` instances.
 *     -   Upon initialization, sends `StartWorkEvent` to all its workers.
 *     -   Polls each worker with `StatusRequestEvent` once a second and collects
 *         `StatusResponseEvent`s.
 *     -   After a predefined number of status checks it runs a TWO-PHASE shutdown: phase one
 *         asks every worker to stop and waits for all the acknowledgements; phase two
 *         `broadcast<qb::KillEvent>()`s, which terminates the workers *and* the supervisor.
 *
 * @note TWO THINGS IN THIS FILE USED TO BE SILENTLY FALSE, and both are worth knowing about.
 *
 *       (1) `on(qb::KillEvent const &)` PRINTED NOTHING, on either actor. Not because the
 *       handler was mis-declared -- both actors do `registerEvent<qb::KillEvent>(*this)` in
 *       `onInit()`, which is exactly what makes a derived handler dispatch -- but because
 *       NOBODY EVER SENT ONE. `kill()` does not post a `KillEvent`; it flips the actor's alive
 *       flag and unregisters it (`qb/src/qb/core/Actor.cpp`). Both actors called `kill()`
 *       directly, so the "graceful termination" the file advertised never ran. The shutdown
 *       below actually sends the event, so both handlers are live.
 *
 *       (2) THE POLLING CADENCE WAS AN ACCIDENT. The worker's per-turn `qb::ICallback` handler
 *       ended in `std::this_thread::sleep_for(500ms)`. That does not schedule anything -- it
 *       freezes the VirtualCore, i.e. all three workers and the supervisor, because they share
 *       core 0. The supervisor's "once per second" polling was simply what fell out of four
 *       actors taking turns being frozen; the run took 7.5s for 5 rounds. Both cadences are now
 *       real timers: `spawn(...)` + `co_await ctx.sleep(d)`, which suspends instead of blocking
 *       and is cancelled automatically when the actor is killed.
 *
 * QB Features Demonstrated:
 * - Supervisor-Worker Pattern: `SupervisorActor` managing `WorkerActor`s.
 * - Two-Phase Shutdown: drain with `ShutdownRequestEvent`/`ShutdownCompleteEvent`, then
 *   terminate with `broadcast<qb::KillEvent>()`.
 * - Status Polling: `StatusRequestEvent` and `StatusResponseEvent` for monitoring.
 * - Event System: Multiple custom events for specific lifecycle and control actions.
 * - System Event Handling: `registerEvent<qb::KillEvent>(*this)` + `on(qb::KillEvent const &)`.
 * - Non-Blocking Periodic Work: `spawn(...)` + `co_await ctx.sleep(...)`.
 * - Actor Communication: `push<EventType>(...)`, `event.getSource()`.
 * - Engine and Actor Management: `qb::Main`, `engine.addActor<ActorType>()`, `kill()`.
 * - Thread-Safe I/O: `qb::io::cout()`.
 */

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>

using namespace std::chrono_literals;

// Helper function to get current timestamp string.
//
// `std::localtime` returns a pointer to a SHARED static `std::tm` and is not thread-safe. This
// function is called on every log line in this file, from the main thread and from core 0's
// worker thread, so the previous version raced on that object several times per second.
// `localtime_r` / `localtime_s` fill a caller-owned struct instead.
std::string
getCurrentTimeString() {
    const auto now = std::chrono::system_clock::now();
    const auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm    tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

// Step 1: Define lifecycle events
struct StartWorkEvent : public qb::Event {};
struct StatusRequestEvent : public qb::Event {};
struct StatusResponseEvent : public qb::Event {
    int processed_count;
    explicit StatusResponseEvent(int count)
        : processed_count(count) {}
};
struct ShutdownRequestEvent : public qb::Event {};
struct ShutdownCompleteEvent : public qb::Event {
    int processed_count;
    explicit ShutdownCompleteEvent(int count)
        : processed_count(count) {}
};

// Self-addressed wake-ups produced by each actor's own coroutine timer.
struct WorkTickEvent : public qb::Event {};
struct PollTickEvent : public qb::Event {};

// Step 2: Create a worker actor with periodic processing
class WorkerActor : public qb::Actor {
private:
    int  _processed_count = 0;
    bool _running         = false;

public:
    qb::io::async::task<bool>
    onInit() override {
        // Register event handlers
        registerEvent<StartWorkEvent>(*this);
        registerEvent<StatusRequestEvent>(*this);
        registerEvent<ShutdownRequestEvent>(*this);
        registerEvent<WorkTickEvent>(*this);
        // Dispatch is by subscription: `qb::Actor`'s constructor already subscribed KillEvent
        // with the BASE handler, and this line replaces that entry with one resolved on
        // `WorkerActor`. Without it, `on(qb::KillEvent const &)` below is dead code and the
        // actor still terminates -- silently, via the base handler.
        registerEvent<qb::KillEvent>(*this);

        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Initialized, waiting for start signal\n";
        co_return true;
    }

    void
    on(StartWorkEvent const &) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Received start command, beginning work\n";

        _running = true;
        scheduleTick();
    }

    void
    on(WorkTickEvent const &) {
        // A tick that arrives after the drain request is simply ignored.
        if (!_running)
            return;

        _processed_count++;

        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Processing item #" << _processed_count << "\n";

        scheduleTick();
    }

    void
    on(StatusRequestEvent const &event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Status requested, processed " << _processed_count
                       << " items\n";

        // Reply to the requester with our status
        push<StatusResponseEvent>(event.getSource(), _processed_count);
    }

    void
    on(ShutdownRequestEvent const &event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Shutdown requested, stopping work\n";

        // Phase one: stop taking new work. The in-flight `ctx.sleep` is left to expire; its tick
        // finds `_running == false` and does nothing.
        _running = false;

        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Drained, acknowledging\n";

        // Acknowledge, and stay alive until the supervisor says otherwise. Killing here would
        // make the phase-two KillEvent unobservable, which is the shape this file used to have.
        push<ShutdownCompleteEvent>(event.getSource(), _processed_count);
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Worker " << id() << ": Kill event received, terminating after "
                       << _processed_count << " items\n";

        // Any last-moment cleanup that RAII does not cover belongs here, before kill().
        kill();
    }

private:
    void
    scheduleTick() {
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(500ms);
            ctx.template push<WorkTickEvent>();
        });
    }
};

// Step 3: Create a supervisor actor to manage workers
class SupervisorActor : public qb::Actor {
private:
    std::vector<qb::ActorId> _worker_ids;
    int                      _status_check_count = 0;
    int                      _pending_acks       = 0;
    const int                _max_checks         = 5;

public:
    explicit SupervisorActor(std::vector<qb::ActorId> worker_ids)
        : _worker_ids(std::move(worker_ids)) {}

    qb::io::async::task<bool>
    onInit() override {
        // Register event handlers
        registerEvent<StatusResponseEvent>(*this);
        registerEvent<ShutdownCompleteEvent>(*this);
        registerEvent<PollTickEvent>(*this);
        registerEvent<qb::KillEvent>(*this);

        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Initialized, managing " << _worker_ids.size() << " workers\n";

        // Start all workers
        for (const auto &worker_id : _worker_ids) {
            qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Starting worker " << worker_id << "\n";
            push<StartWorkEvent>(worker_id);
        }

        schedulePoll();

        co_return true;
    }

    void
    on(StatusResponseEvent const &event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Received status from worker: " << event.getSource()
                       << ", processed count: " << event.processed_count << "\n";
    }

    void
    on(PollTickEvent const &) {
        // Periodically check worker status
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Checking status of all workers\n";

        for (const auto &worker_id : _worker_ids) {
            push<StatusRequestEvent>(worker_id);
        }

        _status_check_count++;

        // After a certain number of checks, initiate shutdown
        if (_status_check_count >= _max_checks) {
            qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Reached max status checks, initiating shutdown\n";

            _pending_acks = static_cast<int>(_worker_ids.size());
            for (const auto &worker_id : _worker_ids) {
                push<ShutdownRequestEvent>(worker_id);
            }
            // No more polling from here on: the shutdown is driven by the acknowledgements.
        } else {
            schedulePoll();
        }
    }

    void
    on(ShutdownCompleteEvent const &event) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Worker " << event.getSource() << " drained after "
                       << event.processed_count << " items (" << (_pending_acks - 1) << " still draining)\n";

        if (--_pending_acks == 0) {
            qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: All workers drained, broadcasting KillEvent\n";
            // Phase two. This reaches every actor in the system INCLUDING this one, so the
            // handler below runs for the supervisor too.
            broadcast<qb::KillEvent>();
        }
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "[" << getCurrentTimeString() << "] Supervisor: Kill event received, terminating\n";
        kill();
    }

private:
    void
    schedulePoll() {
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(1s);
            ctx.template push<PollTickEvent>();
        });
    }
};

int
main() {
    // Create the main QB engine
    qb::Main engine;

    // Create 3 workers on core 0
    std::vector<qb::ActorId> worker_ids;
    for (int i = 0; i < 3; i++) {
        worker_ids.push_back(engine.addActor<WorkerActor>(0));
    }

    // Create supervisor on core 0
    engine.addActor<SupervisorActor>(0, worker_ids);

    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: Starting QB engine\n";
    engine.start();

    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: Waiting for actors to complete\n";
    engine.join();

    qb::io::cout() << "[" << getCurrentTimeString() << "] Main: All actors have terminated, exiting\n";
    return 0;
}
