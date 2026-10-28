/**
 * @file examples/04-patterns/10-spawn-on.cpp
 * @tier 04-patterns
 * @teaches How to create an actor on ANOTHER core while the engine runs: `co_await qb::spawn_on<A>(ctx,
 *          core, timeout, args...)` asks that core's `qb::Spawner` to build it there, and answers once
 *          it is active -- with its id, or with why it is not.
 * @demonstrates qb::spawn_on<Worker>, qb::Spawner, qb::SpawnRequest, qb::spawn_status, qb::spawn_result,
 *               resolve_ask, qb::ask, qb::answer, qb::Request<Answer>, spawn, getIndex, index,
 *               registerEvent<E>, qb::Main, addActor<T>
 * @prerequisites 01-actors/04-cores-and-placement, 04-patterns/04-scatter-gather
 * @expect "[pool] 4 workers spawned at run time: 2 on core 1, 2 on core 2"
 * @expect "[pool] every worker answered from the core it was spawned on, built from its own std::string"
 * @expect "[pool] core 3 runs no VirtualCore: spawn_on answered unreachable once its timeout passed"
 * @expect "=== spawn_on complete: an elastic pool without a factory actor per core ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * `engine.addActor<T>(core)` works before `start()` only, and `addRefActor<T>()` creates on the
 * caller's OWN core. A pool that grows on demand, or a shard placed on whichever core has room,
 * used to need a hand-written factory actor on every core and a request event per actor type.
 * `qb::Spawner` is that factory, once, for every type; `qb::spawn_on` is the request.
 *
 * WHAT CROSSES THE CORE BOUNDARY, AND WHAT DOES NOT
 * -------------------------------------------------
 * The constructor arguments do: `spawn_on` takes them by value, moves them into a type-erased
 * factory on the calling core and into the constructor on the target core -- only a pointer to
 * that factory travels through the mailbox, so a `std::string` is fine. The actor never does: it is
 * built, initialised and run on the target core, by that core's own admission, and only its id
 * comes back. There is no pointer to hold, so there is nothing to call across threads.
 *
 * THE TWO THINGS TO SET UP
 * ------------------------
 * 1. A `qb::Spawner` on every core that accepts run-time creation -- `engine.addActor<qb::Spawner>(c)`
 *    before `start()`. A core without one never answers: `spawn_on` reports `unreachable` once its
 *    timeout passes, which the last section measures against a core the engine does not even run.
 * 2. The CALLING actor registers `qb::SpawnRequest` and resolves it (`resolve_ask`), like any exchange
 *    it `qb::ask`s with. Forget it and the answer is a dead letter: `unreachable`, though the actor
 *    was created all the same.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-spawn-on
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-spawn-on
 */

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>
#include <qb/string.h>

constexpr int        kWorkers    = 4;
constexpr qb::CoreId kFirstCore  = 1; // workers go to cores 1 and 2, alternately
constexpr qb::CoreId kAbsentCore = 3; // the engine runs cores 0, 1 and 2 only
static_assert(kWorkers == 4, "the measured line below counts four workers, two per core");

// What a worker answers: the square it computed, the core it computed it on, and its own name.
struct Answer {
    int            value = 0;
    qb::CoreId     core  = 0;
    qb::string<16> name;
};

// The job, asked with qb::ask: the response slot comes from qb::Request<Answer>.
struct Square : qb::Request<Answer> {
    int n    = 0;
    Square() = default;
    explicit Square(int v)
        : n(v) {}
};

// A worker, built on the core the coordinator chose, from a std::string moved across.
class Worker : public qb::Actor {
    std::string _name;

public:
    explicit Worker(std::string name)
        : _name(std::move(name)) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Square>(*this);
        co_return true;
    }

    void
    on(Square &q) {
        qb::answer(*this, q, [this](Square const &s) {
            Answer a;
            a.value = s.n * s.n;
            a.core  = getIndex();
            a.name  = _name.c_str();
            return a;
        });
    }
};

// The coordinator grows the pool at run time, then uses it.
class Coordinator : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<qb::SpawnRequest>(*this); // so spawn_on's answers resolve here
        registerEvent<Square>(*this);           // so the jobs' answers resolve here
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- grow: four workers, alternately on cores 1 and 2 ------------------------------
            std::vector<qb::spawn_result> pool;
            int                           on_core[3] = {0, 0, 0};
            for (int i = 0; i < kWorkers; ++i) {
                const qb::CoreId core = static_cast<qb::CoreId>(kFirstCore + i % 2);
                auto             w    = co_await qb::spawn_on<Worker>(ctx, core, std::chrono::seconds(2), "shard-" + std::to_string(i));
                if (w.status == qb::spawn_status::spawned && w.id.index() == core)
                    ++on_core[core];
                pool.push_back(w);
            }
            if (on_core[1] == 2 && on_core[2] == 2)
                qb::io::cout() << "[pool] 4 workers spawned at run time: 2 on core 1, 2 on core 2\n";
            else
                qb::io::cout() << "[pool] spawn FAILED: " << on_core[1] << " on core 1, " << on_core[2] << " on core 2\n";

            // ---- use: one job each; every answer says where it was computed -----------------
            int right = 0;
            for (int i = 0; i < kWorkers; ++i) {
                if (!pool[static_cast<std::size_t>(i)])
                    continue;
                const auto id = pool[static_cast<std::size_t>(i)].id;
                const auto r  = co_await qb::ask(ctx, id, Square{i + 2}, std::chrono::seconds(2));
                const bool ok = r.response.value == (i + 2) * (i + 2) && r.response.core == id.index()
                                && std::string(r.response.name.c_str()) == "shard-" + std::to_string(i);
                right += ok ? 1 : 0;
            }
            if (right == kWorkers)
                qb::io::cout() << "[pool] every worker answered from the core it was spawned on, built from its own std::string\n";
            else
                qb::io::cout() << "[pool] only " << right << " of " << kWorkers << " workers answered as built\n";

            // ---- the caveat: a core with no Spawner -- here, one the engine does not run --------
            const auto absent = co_await qb::spawn_on<Worker>(ctx, kAbsentCore, std::chrono::milliseconds(200), std::string("nowhere"));
            if (absent.status == qb::spawn_status::unreachable)
                qb::io::cout() << "[pool] core 3 runs no VirtualCore: spawn_on answered unreachable once its timeout passed\n";
            else
                qb::io::cout() << "[pool] core 3: unexpected status " << static_cast<int>(absent.status) << "\n";

            qb::io::cout() << "=== spawn_on complete: an elastic pool without a factory actor per core ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    void
    on(qb::SpawnRequest &e) {
        resolve_ask(e);
    }

    void
    on(Square &e) {
        resolve_ask(e);
    }
};

int
main() {
    qb::Main engine;
    engine.addActor<qb::Spawner>(1);
    engine.addActor<qb::Spawner>(2);
    engine.addActor<Coordinator>(0);

    qb::io::cout() << "[main] three cores, two spawners, no worker before start()\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
