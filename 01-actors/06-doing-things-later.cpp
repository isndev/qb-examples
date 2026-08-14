/**
 * @file examples/01-actors/06-doing-things-later.cpp
 * @tier 01-actors
 * @teaches The four ways to run work later and which one is right when — and that inside an
 *          actor the answer is a coroutine sleep that the framework cancels when the actor dies,
 *          not a blocked handler and not a per-turn callback.
 * @demonstrates spawn, qb::ScopedCoroContext, ctx.sleep, qb::io::async::task<void>,
 *               registerEvent<E>, qb::KillEvent, push<E>, getSource(), broadcast<qb::KillEvent>,
 *               kill(), qb::Main, addActor<T>
 * @prerequisites 01-actors/02-messaging
 * @expect "Application stopping slow_timer..."
 * @expect "Application shutting down all actors..."
 * @expect "Example completed."
 *
 * @example Timers and Delayed Actions
 *
 * @brief This example demonstrates how to run work after a delay, and repeatedly on an
 * interval, inside the QB Actor Framework -- using the framework's own timers.
 *
 * @details
 * The system includes:
 * 1.  `TimerManager` Actor:
 *     -   Receives `StartTimerMsg` to arm a named, repeating timer on behalf of a caller.
 *     -   Arms each interval with `spawn(...)` + `co_await ctx.sleep(interval)`, which wakes the
 *         actor with a `TimerTickMsg` and is cancelled automatically if the actor is killed.
 *     -   Emits a `TimerFiredMsg` to the actor that requested the timer on every tick.
 *     -   Handles `CancelTimerMsg` to stop an existing timer.
 * 2.  `Application` Actor:
 *     -   Asks the `TimerManager` to start a fast repeating timer and a slow endless one.
 *     -   Receives `TimerFiredMsg` and logs it.
 *     -   Sequences its own steps with the same delay mechanism.
 *     -   Initiates a system-wide shutdown using `broadcast<qb::KillEvent>()`.
 *
 * Expected schedule (and the run's wall time should match it -- that is the point):
 *     t=0.0s   fast_timer armed (500ms x 5), slow_timer armed (2000ms, endless)
 *     t=0.5s   .. t=2.5s   fast_timer fires 5 times and completes
 *     t=2.0s, 4.0s         slow_timer fires
 *     t=5.0s   step 1: cancel slow_timer
 *     t=6.5s   step 2
 *     t=8.0s   step 3: broadcast KillEvent, engine drains, process exits
 *
 * @note WHAT THIS FILE USED TO DO, because it is the mistake it now exists to prevent.
 *       Every "delay" was a plain `push<DelayedActionMsg>(id(), action, delay_ms)`. `push` has
 *       no delay parameter; the `delay_ms` field was written by the sender and READ BY NOBODY.
 *       So a file advertising 500ms / 2000ms / 5000ms ran to completion in 32 MILLISECONDS,
 *       `fast_timer` got through 3 of its declared 5 repetitions before the shutdown overtook
 *       it, and the whole thing exited 0. It also pushed its `TimerFiredMsg` to `id()` -- the
 *       TimerManager's own id, not the requester's -- so `Application::on(TimerFiredMsg&)` was
 *       unreachable and printed nothing at all.
 *
 * @note QB OFFERS FOUR WAYS TO DO SOMETHING LATER. Pick deliberately:
 *       - `spawn(f)` + `co_await ctx.sleep(d)` -- what this file uses. A real timer, bound to
 *         the actor's cancellation scope: if the actor is killed while the sleep is pending the
 *         coroutine unwinds instead of resuming into freed memory. Default choice.
 *       - `qb::io::async::callback(f, d)` -- a real timer that is NOT bound to any actor's
 *         lifetime. `f` still runs after the actor is destroyed, so `[this]` captures are a
 *         use-after-free waiting to happen. Use it only for work that must deliberately outlive
 *         the actor.
 *       - `qb::io::async::defer(f)` -- runs `f` at the end of the current loop turn. For
 *         breaking re-entrancy, not for waiting. (Note that `qb::io::async::callback(f)` with NO
 *         duration is not deferral at all: it calls `f()` inline, right now.)
 *       - `qb::ICallback` + `on(qb::LoopEvent const &)` -- runs on EVERY turn of the core's
 *         event loop, microseconds apart. A per-turn hook, never a timer.
 *       And in none of them may you `std::this_thread::sleep_for`: that blocks the VirtualCore
 *       thread, freezing every actor, timer and socket it owns.
 *
 * QB Features Demonstrated:
 * - Delays and Intervals: `spawn(...)` + `co_await ctx.sleep(...)`, and `ctx.push<T>(...)` to
 *   return to actor context.
 * - Coroutine Capture Discipline: copy by value before the first `co_await`, never `this`.
 * - Actor Communication: `push<EventType>(...)`, `event.getSource()` to answer the requester.
 * - Coordinated Shutdown: `broadcast<qb::KillEvent>()` plus `registerEvent<qb::KillEvent>()`.
 * - Engine Management: `qb::Main`, `engine.addActor<ActorType>()`, `engine.start()`, `engine.join()`.
 */

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <qb/actor.h>
#include <qb/event.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// NOTE ON EVENT PAYLOADS, which governs every event below: the engine relocates an event with
// `memcpy` and never runs the source destructor, so a payload member may hold no pointer into
// itself. On libstdc++ a SHORT std::string holds exactly that -- `_M_p` addresses its own inline
// buffer -- so after the relocation it still points at the old storage. libc++ recomputes the
// pointer from `this`, which is why the defect is invisible on macOS and corrupts on Linux.
// Relocation is not a cross-core-only event: pipe growth, compaction, `reply()` and `forward()`
// relocate same-core events too, and this example pushes several of these to itself.

// Message to start a timer
struct StartTimerMsg : public qb::Event {
    std::chrono::milliseconds interval;
    int                       repeat_count; // 0 = repeat forever
    qb::string<32>            timer_name;

    StartTimerMsg(const std::chrono::milliseconds &i, int r, std::string_view name)
        : interval(i)
        , repeat_count(r)
        , timer_name(name) {}
};

// Self-addressed wake-up: one of the TimerManager's sleeps has elapsed.
struct TimerTickMsg : public qb::Event {
    qb::string<32> timer_name;
    uint64_t       generation; // guards against a tick from a cancelled-then-restarted timer

    TimerTickMsg(std::string_view name, uint64_t gen)
        : timer_name(name)
        , generation(gen) {}
};

// Message emitted when a timer fires
struct TimerFiredMsg : public qb::Event {
    qb::string<32> timer_name;
    int            count;

    TimerFiredMsg(std::string_view name, int c)
        : timer_name(name)
        , count(c) {}
};

// Message to cancel a timer
struct CancelTimerMsg : public qb::Event {
    qb::string<32> timer_name;

    explicit CancelTimerMsg(std::string_view name)
        : timer_name(name) {}
};

// Self-addressed wake-up for the Application's own demo steps.
struct AppStepMsg : public qb::Event {
    enum class Step { CANCEL_SLOW_TIMER, WAIT_FOR_TIMERS, SHUT_DOWN };

    Step step;

    explicit AppStepMsg(Step s)
        : step(s) {}
};

// Actor that manages timers
class TimerManager : public qb::Actor {
public:
    TimerManager() {
        registerEvent<StartTimerMsg>(*this);
        registerEvent<TimerTickMsg>(*this);
        registerEvent<CancelTimerMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "TimerManager " << id() << ": Initialized\n";
        co_return true;
    }

    void
    on(StartTimerMsg &msg) {
        qb::io::cout() << "Starting timer '" << msg.timer_name << "' with interval " << msg.interval.count() << "ms, repeat "
                       << msg.repeat_count << " times\n";

        // The event's name is a fixed-size qb::string, so it is converted once here to the
        // std::string this actor's own (never relocated) map is keyed by.
        TimerInfo info;
        info.interval      = msg.interval;
        info.repeat_count  = msg.repeat_count;
        info.current_count = 0;
        info.owner         = msg.getSource(); // answer whoever asked, not ourselves
        info.generation    = ++_next_generation;

        const std::string name = msg.timer_name.c_str();
        _timers[name]          = info;

        armNext(name, info);
    }

    void
    on(TimerTickMsg &msg) {
        const std::string name = msg.timer_name.c_str();

        auto it = _timers.find(name);
        // Cancelled while the sleep was pending, or a stale tick from an earlier arming.
        if (it == _timers.end() || it->second.generation != msg.generation)
            return;

        TimerInfo &info = it->second;
        info.current_count++;

        // Fire the timer event -- to the requester.
        push<TimerFiredMsg>(info.owner, name, info.current_count);
        qb::io::cout() << "Timer '" << name << "' fired, count: " << info.current_count << std::endl;

        if (info.repeat_count > 0 && info.current_count >= info.repeat_count) {
            qb::io::cout() << "Timer '" << name << "' completed all " << info.repeat_count << " repetitions\n";
            _timers.erase(it);
        } else {
            armNext(name, info);
        }
    }

    void
    on(CancelTimerMsg &msg) {
        auto it = _timers.find(msg.timer_name.c_str());
        if (it != _timers.end()) {
            qb::io::cout() << "Cancelling timer '" << msg.timer_name << "'\n";
            // Erasing is enough: the pending sleep still expires, but its tick finds no entry
            // (and, if the name is reused, a different generation) and is dropped.
            _timers.erase(it);
        }
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "TimerManager: Shutting down\n";

        // Any pending `ctx.sleep` is cancelled by kill() -- the coroutines unwind rather than
        // resuming into a destroyed actor.
        _timers.clear();

        kill();
    }

private:
    struct TimerInfo {
        std::chrono::milliseconds interval{};
        int                       repeat_count{0};
        int                       current_count{0};
        qb::ActorId               owner{};
        uint64_t                  generation{0};
    };

    void
    armNext(const std::string &name, const TimerInfo &info) {
        // Copy everything the coroutine needs BEFORE spawning. It must not read an actor member
        // after `co_await`: the actor may be gone by then, and `_timers` certainly may have
        // rehashed.
        const qb::string<32> tick_name{name};
        const auto           interval   = info.interval;
        const auto           generation = info.generation;

        spawn([tick_name, interval, generation](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(interval);
            ctx.template push<TimerTickMsg>(std::string_view{tick_name.c_str()}, generation);
        });
    }

    std::unordered_map<std::string, TimerInfo> _timers;
    uint64_t                                   _next_generation{0};
};

// Actor representing our application
class Application : public qb::Actor {
public:
    explicit Application(qb::ActorId timer_manager_id)
        : _timer_manager_id(timer_manager_id) {
        registerEvent<AppStepMsg>(*this);
        registerEvent<TimerFiredMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "Application starting...\n";

        // Start a fast timer that repeats 5 times
        push<StartTimerMsg>(_timer_manager_id, 500ms, 5, "fast_timer");

        // Start a slow timer that repeats indefinitely (repeat_count = 0)
        push<StartTimerMsg>(_timer_manager_id, 2000ms, 0, "slow_timer");

        // Let the timers run for a while before the first application step.
        scheduleStep(AppStepMsg::Step::CANCEL_SLOW_TIMER, 5000ms);

        co_return true;
    }

    void
    on(TimerFiredMsg &msg) {
        qb::io::cout() << "Application received timer event: " << msg.timer_name << " fired (count: " << msg.count << ")" << std::endl;
    }

    void
    on(AppStepMsg &msg) {
        switch (msg.step) {
            case AppStepMsg::Step::CANCEL_SLOW_TIMER:
                qb::io::cout() << "Application stopping slow_timer...\n";
                push<CancelTimerMsg>(_timer_manager_id, "slow_timer");
                scheduleStep(AppStepMsg::Step::WAIT_FOR_TIMERS, 1500ms);
                break;

            case AppStepMsg::Step::WAIT_FOR_TIMERS:
                qb::io::cout() << "Application waiting for timers to complete...\n";
                scheduleStep(AppStepMsg::Step::SHUT_DOWN, 1500ms);
                break;

            case AppStepMsg::Step::SHUT_DOWN:
                qb::io::cout() << "Application shutting down all actors...\n";
                broadcast<qb::KillEvent>();
                break;
        }
    }

    void
    on(qb::KillEvent const &) {
        qb::io::cout() << "Application: Shutting down\n";
        kill();
    }

private:
    void
    scheduleStep(AppStepMsg::Step step, qb::duration delay) {
        spawn([step, delay](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(delay);
            ctx.template push<AppStepMsg>(step);
        });
    }

    qb::ActorId _timer_manager_id;
};

int
main() {
    // Create the main engine
    qb::Main engine;

    // Create actors
    auto timer_manager_id = engine.addActor<TimerManager>(0);
    engine.addActor<Application>(0, timer_manager_id);

    qb::io::cout() << "Main: Starting QB engine\n";
    engine.start();

    qb::io::cout() << "Main: Waiting for actors to complete\n";
    engine.join();

    qb::io::cout() << "Example completed.\n";
    return 0;
}
