/**
 * @file examples/04-patterns/02-supervisor.cpp
 * @tier 04-patterns
 * @teaches Let something else restart your actors. `qb::Supervisor` owns a fixed set of child
 *          slots, restarts them by a declared `restart_strategy` when one terminates, ignores
 *          a stale report from an already-replaced child, and escalates instead of restarting
 *          forever once a restart-intensity cap is exceeded.
 * @demonstrates qb::Supervisor, qb::SupervisedActor, qb::ChildDown, qb::restart_strategy,
 *               spawn_child, on_escalate, supervisor(), stop(), child, restarts,
 *               child_count, addRefActor<T>, qb::KillEvent, registerEvent<E>, push<E>
 * @prerequisites 01-actors/05-lifecycle, 04-patterns/01-pubsub
 * @expect "[lab] one_for_one + a stale ChildDown: 3 initial + 1 restart = "
 * @expect "[lab] one_for_all, crash slot 1: 3 initial + 3 restarts = "
 * @expect "[lab] rest_for_one, crash slot 1: 3 initial + 2 restarts = "
 * @expect "[lab] intensity cap: 1 initial + 2 restarts, then on_escalate() = "
 * @expect "=== supervision complete: 4 policies, no restart bookkeeping written by hand ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * The hand-rolled supervisor half of `examples/01-actors/05-lifecycle.cpp`, whose
 * `SupervisorActor` tracks its workers in its own vector, POLLS them for status, and has no
 * answer at all to the two questions that make supervision hard: what else should restart when
 * this one dies, and when should we stop restarting? Those are the two arguments of
 * `qb::Supervisor`'s constructor. (That file keeps its lifecycle half — `kill`, `KillEvent`,
 * ordered teardown — which is a tier-1 lesson and not this one.)
 *
 * THE MODEL, IN FOUR SENTENCES
 * ----------------------------
 * A `Supervisor` has `child_count` SLOTS, and `spawn_child(slot, generation)` is the one method
 * you write: it creates the child and returns its id. A child derives `qb::SupervisedActor`
 * (which knows its supervisor, slot and generation) and calls `stop()` to terminate
 * cooperatively — that sends a `ChildDown` carrying its slot and generation. The supervisor
 * restarts per its `restart_strategy`, bumping the slot's generation so a `ChildDown` from the
 * outgoing child is recognised as stale and ignored. Killing the supervisor kills its children
 * first, so nothing is orphaned.
 *
 * WHAT SUPERVISION HERE IS NOT
 * ----------------------------
 * It is COOPERATIVE. A child that dies without calling `stop()` — one killed by someone else,
 * or whose `onInit()` returned false — sends no `ChildDown`, so nothing restarts it. That is
 * stated in the header (`supervisor.h`) and it is the single most important thing to know
 * before designing around this class: supervision keys off the notification, not off death.
 *
 * The four phases below run one at a time, each on a fresh supervisor created by the Lab with
 * `addRefActor`, so the spawn totals are exact and the output reads top to bottom. The totals
 * are what tell the three strategies apart, and they are the whole lesson:
 *   one_for_one  restart the child that went down                   3 + 1 = 4
 *   one_for_all  restart every child when any one goes down         3 + 3 = 6
 *   rest_for_one restart it and everything started after it         3 + 2 = 5
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-supervisor
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-supervisor
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

using namespace std::chrono_literals;

// Lab -> supervisor: crash whatever child is currently in this slot.
struct TriggerCrash : public qb::Event {
    std::size_t slot;
    explicit TriggerCrash(std::size_t s)
        : slot(s) {}
};

// Supervisor -> child: terminate cooperatively.
struct Crash : public qb::Event {};

// Child -> Lab: "slot S is up, live as `who`". One per spawn, so counting them counts restarts.
struct SpawnAck : public qb::Event {
    std::size_t slot;
    qb::ActorId who;
    SpawnAck(std::size_t s, qb::ActorId w)
        : slot(s)
        , who(w) {}
};

// Supervisor -> Lab: the restart-intensity cap was exceeded; nothing was restarted.
struct Escalated : public qb::Event {};

// ---------------------------------------------------------------------------
// A supervised child. It derives SupervisedActor, not Actor, and the only thing that buys is
// `stop()` — notify the supervisor, then die.
// ---------------------------------------------------------------------------
class Worker : public qb::SupervisedActor {
    qb::ActorId _lab;
    std::size_t _slot;

public:
    Worker(qb::ActorId sup, std::size_t slot, std::uint64_t generation, qb::ActorId lab)
        : qb::SupervisedActor(sup, slot, generation)
        , _lab(lab)
        , _slot(slot) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Crash>(*this);
        // Announcing every spawn is what makes the restart counts observable; a real worker
        // would just start working here. `supervisor()` is the id SupervisedActor was given.
        push<SpawnAck>(_lab, _slot, id());
        (void) supervisor();
        co_return true;
    }

    void
    on(Crash const &) {
        // Cooperative termination: ChildDown to the supervisor, then kill(). A worker that just
        // called kill() here would NOT be restarted — see the header note above.
        stop();
    }
};

// ---------------------------------------------------------------------------
// The supervisor. `spawn_child` is the only method a user of this class must write.
// ---------------------------------------------------------------------------
class WorkerSupervisor : public qb::Supervisor {
    qb::ActorId _lab;
    bool        _probe_stale;

public:
    WorkerSupervisor(qb::restart_strategy strategy, std::size_t children, qb::ActorId lab, unsigned max_restarts, qb::duration window,
                     bool probe_stale)
        : qb::Supervisor(strategy, children, max_restarts, window)
        , _lab(lab)
        , _probe_stale(probe_stale) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<TriggerCrash>(*this);
        // The base onInit registers ChildDown + KillEvent and spawns the initial children, so it
        // must run — and `co_await`ing it is how a derived onInit composes with it.
        const bool ok = co_await qb::Supervisor::onInit();
        if (_probe_stale) {
            // THE GENERATION GUARD, provoked rather than described. A `ChildDown` is what a child
            // sends when it calls `stop()`; this one carries generation 999, which no live slot
            // has, so it MUST be ignored. The phase's printed spawn total is the oracle: an
            // honoured stale report would restart slot 0 and make that total one higher.
            push<qb::ChildDown>(id(), std::size_t{0}, std::uint64_t{999});
        }
        co_return ok;
    }

    void
    on(TriggerCrash const &e) {
        push<Crash>(child(e.slot)); // `child(slot)` is always the CURRENT occupant of that slot
    }

protected:
    // Called by the framework for the initial children and for every restart. The generation is
    // the supervisor's stale-report guard; hand it straight back to the child.
    qb::ActorId
    spawn_child(std::size_t slot, std::uint64_t generation) override {
        return addRefActor<Worker>(id(), slot, generation, _lab).id();
    }

    // Called instead of restarting once `max_restarts` is exceeded. The default is a no-op, so
    // an unhandled escalation silently stops supervising — override it and say something.
    void
    on_escalate() override {
        qb::io::cout() << "[sup] restart intensity exceeded after " << restarts() << " restarts of " << child_count()
                       << " slot(s) — escalating\n";
        push<Escalated>(_lab);
    }
};

// ---------------------------------------------------------------------------
// Runs the four phases in order. Each phase gets a fresh supervisor so its spawn total is
// exact; crashes are driven off the acks, never off a clock, so nothing here can race.
// ---------------------------------------------------------------------------
class Lab : public qb::Actor {
    struct Phase {
        qb::restart_strategy strategy;
        std::size_t          children;
        int                  crashes; ///< how many times to crash slot 1 (or 0 for a 1-slot phase)
        std::size_t          crash_slot;
        unsigned             max_restarts;  ///< 0 = unlimited
        int                  expected_acks; ///< initial spawns + expected restarts
        const char          *label;
    };

    // The three strategies differ ONLY in this table, which is the point.
    static constexpr Phase kPhases[] = {
        // Phase 1 also receives one STALE `qb::ChildDown` that must be ignored, so its total of 4
        // proves the generation guard as well as the strategy.
        {qb::restart_strategy::one_for_one, 3, 1, 1, 0, 4, "[lab] one_for_one + a stale ChildDown: 3 initial + 1 restart = "},
        {qb::restart_strategy::one_for_all, 3, 1, 1, 0, 6, "[lab] one_for_all, crash slot 1: 3 initial + 3 restarts = "},
        {qb::restart_strategy::rest_for_one, 3, 1, 1, 0, 5, "[lab] rest_for_one, crash slot 1: 3 initial + 2 restarts = "},
        // 1 child, cap 2 restarts inside a 1 s window: crash it three times and the third
        // ChildDown escalates instead of restarting, so the acks stop at 3.
        {qb::restart_strategy::one_for_one, 1, 3, 0, 2, 3, "[lab] intensity cap: 1 initial + 2 restarts, then on_escalate() = "},
    };
    static constexpr int kPhaseCount = static_cast<int>(std::size(kPhases));

    int         _phase   = 0;
    int         _acks    = 0;
    int         _crashes = 0;
    qb::ActorId _sup;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<SpawnAck>(*this);
        registerEvent<Escalated>(*this);
        start_phase();
        co_return true;
    }

    void
    on(SpawnAck const &) {
        ++_acks;
        auto const &p = kPhases[_phase];
        // Crash again only once the previous restart has completed — the ack IS that completion,
        // so the phases are causally ordered with no sleep anywhere in this program.
        if (_acks >= static_cast<int>(p.children) && _crashes < p.crashes) {
            ++_crashes;
            push<TriggerCrash>(_sup, p.crash_slot);
        }
        if (_acks == p.expected_acks && p.max_restarts == 0)
            finish_phase();
    }

    void
    on(Escalated const &) {
        finish_phase(); // the capped phase ends on the escalation, not on an ack count
    }

private:
    void
    start_phase() {
        auto const &p = kPhases[_phase];
        _acks         = 0;
        _crashes      = 0;
        _sup          = addRefActor<WorkerSupervisor>(p.strategy, p.children, id(), p.max_restarts,
                                                      p.max_restarts ? qb::duration{1s} : qb::duration::zero(),
                                                      /*probe_stale=*/_phase == 0)
                            .id();
    }

    void
    finish_phase() {
        qb::io::cout() << kPhases[_phase].label << _acks << " spawns\n";
        // Killing the supervisor tears down its children first — no orphans, and the next phase
        // starts from a clean core.
        push<qb::KillEvent>(_sup);
        if (++_phase < kPhaseCount) {
            start_phase();
            return;
        }
        qb::io::cout() << "=== supervision complete: 4 policies, no restart bookkeeping written by hand ===\n";
        qb::Main::stop();
    }
};

int
main() {
    qb::Main engine;
    engine.addActor<Lab>(0);

    qb::io::cout() << "[main] one supervisor at a time, four restart policies\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
