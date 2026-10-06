/**
 * @file examples/01-actors/13-death-watch.cpp
 * @tier 01-actors
 * @teaches Learn that another actor is gone -- really gone, its destructor run -- whatever ended it,
 *          on any core, without its cooperation: `watch()` it, and one `qb::DownEvent` arrives,
 *          carrying why. `unwatch()` takes it back, even when the answer is already on its way.
 * @demonstrates watch, unwatch, qb::DownEvent, qb::DownReason, qb::down_reason_name,
 *               addRefActor<T>, addActor<T>, registerEvent<E>, push<E>, qb::KillEvent, getSource()
 * @prerequisites 01-actors/05-lifecycle, 01-actors/08-child-actors
 * @expect "[lab] same core, kill(): killed, after the destructor"
 * @expect "[lab] same core, onInit() returned false: init_failed"
 * @expect "[lab] core 1, kill(): killed, after the destructor"
 * @expect "[lab] core 1, an actor already gone: unknown, at once"
 * @expect "[lab] unwatch() with the answer on its way: dropped -- the next DownEvent is the second worker's"
 * @expect "=== death watch: five ends, each answered once ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * Before 3.3 an actor learned that another had died only if the dying one said so -- a
 * `ChildDown` from a `SupervisedActor::stop()`, a "bye" event of your own -- or by pinging it on a
 * timer and calling a missing answer death. The first misses every end the actor did not choose
 * (a `kill()` from elsewhere, an `onInit()` that failed or threw, the engine stopping); the second
 * is a timeout pretending to be a fact. A watch is answered by the framework, from the dead actor's
 * own core, after its destructor ran: what it held is released by the time you hear of it.
 *
 * THE RULES, IN FIVE SENTENCES
 * ----------------------------
 * `watch(id)` from any actor, of an actor on any core; register `qb::DownEvent` to receive the
 * answer, or it is an `unhandled` dead letter. Every watch is answered exactly once: `killed`,
 * `init_failed`, `init_threw`, `unknown` (no actor held the id when the watch arrived) or
 * `core_stopped` (its core had stopped, or ended on an exception). Watching an actor you already
 * watch is a no-op until the answer arrives. `unwatch(id)` guarantees that no `DownEvent` of that
 * watch arrives afterwards -- not even one already on its way. A watch ends with its watcher: a
 * watcher that dies first is never told, and costs the watched actor nothing.
 *
 * ONE THING TO KNOW ABOUT IDS
 * ---------------------------
 * An id is reused once its actor is gone -- the replacement you spawn next is likely to get it.
 * To watch the replacement of an actor whose answer may still be on its way, `unwatch()` the old
 * one first; then the old answer is dropped, and the new watch waits for the new actor. That is
 * what `qb::Supervisor` does in `qb::supervision::watch` mode (`04-patterns/02-supervisor`).
 *
 * The five phases run one after the other, each started by the previous answer -- no clock
 * anywhere, so the output reads top to bottom on every run.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-death-watch
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-death-watch
 */

#include <atomic>
#include <chrono>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>

using namespace std::chrono_literals;

// How many workers have been destroyed: a watcher compares it before and after its DownEvent.
std::atomic<int> g_destroyed{0};

// Does nothing but live; its destructor is what a DownEvent comes after.
class Worker : public qb::Actor {
public:
    ~Worker() override {
        ++g_destroyed;
    }
};

struct Quit : qb::Event {};
struct Bye : qb::Event {};

// A worker that says goodbye before it dies: its `Bye` reaches the watcher ahead of the answer to
// the watch, which leaves only once the worker has been destroyed, at the end of that pass.
class Teller : public Worker {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quit>(*this);
        co_return true;
    }
    void
    on(Quit const &e) {
        push<Bye>(e.getSource());
        kill();
    }
};

// An actor whose onInit() fails -- after a suspension, so it is alive, activating, when watched.
class Doomed : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        co_await context().sleep(1ms);
        co_return false;
    }
};

// Keeps core 1 running after its worker is gone: a core with no actor left stops, and an id on a
// stopped core is answered `core_stopped` rather than `unknown`.
class Keeper : public qb::Actor {};

class Lab : public qb::Actor {
    const qb::ActorId _remote; // a Worker on core 1
    int               _phase            = 0;
    int               _destroyed_before = 0;
    qb::ActorId       _first{};
    qb::ActorId       _second{};

public:
    explicit Lab(qb::ActorId remote)
        : _remote(remote) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<qb::DownEvent>(*this);
        registerEvent<Bye>(*this);
        start(_phase);
        co_return true;
    }

    void
    on(qb::DownEvent const &e) {
        const bool after_destructor = g_destroyed.load() > _destroyed_before;
        switch (_phase) {
            case 0:
                verdict(e.reason == qb::DownReason::killed && after_destructor, "[lab] same core, kill(): killed, after the destructor", e);
                break;
            case 1:
                verdict(e.reason == qb::DownReason::init_failed, "[lab] same core, onInit() returned false: init_failed", e);
                break;
            case 2:
                verdict(e.reason == qb::DownReason::killed && after_destructor, "[lab] core 1, kill(): killed, after the destructor", e);
                break;
            case 3:
                verdict(e.reason == qb::DownReason::unknown, "[lab] core 1, an actor already gone: unknown, at once", e);
                break;
            case 4:
                // The first worker died first, and its answer was on its way when it was taken back:
                // had it not been dropped, it would be this one.
                verdict(e.watched == _second && e.reason == qb::DownReason::killed,
                        "[lab] unwatch() with the answer on its way: dropped -- the next DownEvent is the second worker's", e);
                break;
        }
        if (++_phase < 5) {
            start(_phase);
            return;
        }
        qb::io::cout() << "=== death watch: five ends, each answered once ===\n";
        qb::Main::stop();
        kill();
    }

    // Phase 4: the first worker is gone, and the answer to its watch is queued behind this `Bye`.
    void
    on(Bye const &) {
        unwatch(_first);
        push<qb::KillEvent>(_second);
    }

private:
    // The phase's verdict: its line, whole, when what it states was observed -- or what was seen instead.
    static void
    verdict(bool const held, const char *const line, qb::DownEvent const &e) {
        if (held)
            qb::io::cout() << line << "\n";
        else
            qb::io::cout() << "[lab] UNEXPECTED " << e.watched << " " << qb::down_reason_name(e.reason) << ", wanted: " << line << "\n";
    }

    void
    start(int const phase) {
        _destroyed_before = g_destroyed.load();
        switch (phase) {
            case 0: { // an actor of this core, killed: the answer comes from this core
                const auto w = addRefActor<Worker>().id();
                watch(w);
                push<qb::KillEvent>(w);
                break;
            }
            case 1: // an actor that never activates: watched while its onInit() is suspended
                watch(addRefActor<Doomed>().id());
                break;
            case 2: // an actor of another core: the watch travels there, then the kill -- both
                    // pushed, so in that order
                watch(_remote);
                push<qb::KillEvent>(_remote);
                break;
            case 3: // the same id again: no actor holds it now, and core 1 says so
                watch(_remote);
                break;
            case 4: // taken back with its answer already on its way: see on(Bye)
                _first  = addRefActor<Teller>().id();
                _second = addRefActor<Worker>().id();
                watch(_first);
                watch(_second);
                push<Quit>(_first);
                break;
        }
    }
};

int
main() {
    qb::Main   engine;
    const auto remote = engine.addActor<Worker>(1);
    engine.addActor<Keeper>(1);
    engine.addActor<Lab>(0, remote);

    qb::io::cout() << "[main] one watcher on core 0, five ways for an actor to end\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
