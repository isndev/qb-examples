/**
 * @file examples/04-patterns/09-discovery.cpp
 * @tier 04-patterns
 * @teaches How one actor finds another it was never handed: `co_await qb::require<T>(ctx, w)`
 *          discovers every live actor of a type across every core, `co_await qb::ping(ctx, id)`
 *          asks one of them whether it is still there — and `is_actor_alive(id)` answers a
 *          DIFFERENT, core-local question that will lie to you about a remote actor.
 * @demonstrates qb::require<Worker>, qb::ping, is_actor_alive, qb::RequireEvent,
 *               resolve_require, is<Worker>, qb::ActorId, index, is_valid, spawn,
 *               ctx.push_to<Compare>, ctx.id, registerEvent<E>, qb::Main, addActor<T>
 * @prerequisites 01-actors/04-cores-and-placement, 04-patterns/04-scatter-gather
 * @expect "[scout] VERDICT discovery crossed the core boundary: 4 workers on 2 cores"
 * @expect "[scout] VERDICT ping answered for both live workers and timed out on the invalid id"
 * @expect "[scout] VERDICT is_actor_alive: true for the LOCAL worker, FALSE for the live remote"
 * @expect "[legacy] require<Worker, Cache>() replies: "
 * @expect "=== discovery complete: two questions, and only one of them crosses a core ==="
 *
 * THE ONE THING THAT WILL BITE YOU
 * --------------------------------
 * `is_actor_alive(id)` and `co_await qb::ping(ctx, id)` look like the same question and are not.
 *
 *   `is_actor_alive(id)`  ONE HASH LOOKUP IN THIS CORE'S ACTOR MAP. Instant, free, and
 *                         SAME-CORE ONLY: an actor map belongs to its VirtualCore and is not
 *                         synchronized, so a `false` for a remote id means "not here", never
 *                         "not alive". It is for pruning a registry of ids you own, on your own
 *                         core — which is exactly what `qb::PubSub` uses it for.
 *   `co_await qb::ping()` A REAL MESSAGE AND A REAL REPLY, so it crosses cores, costs a round
 *                         trip, and can time out. It is the only honest answer to "is that
 *                         actor, over there, still alive?"
 *
 * This program prints both for the same pair of live actors: the remote one is alive, `ping`
 * says so, and `is_actor_alive` says the opposite. Neither is a bug; they answer different
 * questions, and reaching for the cheap one across a core boundary is a class of defect that
 * looks like a race and is not.
 *
 * THE MODERN FORM AND THE LEGACY ONE
 * ----------------------------------
 * `co_await qb::require<T>(ctx, window)` broadcasts a typed `PingEvent`, collects the
 * `RequireEvent` replies for the whole window, and hands back the ids. It needs no handler at
 * all: `qb::Actor` routes discovery replies by default.
 *
 * The legacy form — `require<T...>()`, then `on(RequireEvent&)` with `is<T>(event)` — is
 * fire-and-forget: it tells you nothing about when discovery is over, so you end up inventing a
 * settle window anyway. It is still here, and `LegacyScout` below uses it, for one reason worth
 * knowing: IF YOU OVERRIDE `on(RequireEvent&)` YOU MUST CALL `resolve_require(e)` FIRST, or your
 * override silently eats the replies belonging to every `co_await qb::ping` / `qb::require` in
 * that same actor. That is the only trap in this file that costs anything.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-discovery
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-discovery
 */

#include <chrono>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

using namespace std::chrono_literals;

// Two actor TYPES, because discovery is by type. Neither registers anything: the kernel already
// answers PingEvent for every actor, so both are discoverable out of the box.
class Worker : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        co_return true;
    }
};

class Cache : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        co_return true;
    }
};

// The coroutine hands two discovered ids back to actor context, because `is_actor_alive` is an
// Actor method and a spawned coroutine must not capture `this`.
struct Compare : public qb::Event {
    qb::ActorId local;
    qb::ActorId remote;
    Compare(qb::ActorId l, qb::ActorId r)
        : local(l)
        , remote(r) {}
};
struct StartLegacy : public qb::Event {};
struct LegacyReport : public qb::Event {};

// ---------------------------------------------------------------------------
// The modern scout: discovery and liveness, both awaited, no handler boilerplate.
// ---------------------------------------------------------------------------
class Scout : public qb::Actor {
    qb::ActorId _legacy;

public:
    explicit Scout(qb::ActorId legacy)
        : _legacy(legacy) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Compare>(*this);
        // NOTE what is NOT here: no registerEvent<qb::RequireEvent>, no on(RequireEvent&).
        // `qb::Actor` routes discovery replies to the awaiting coroutine by itself.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- who is out there? --------------------------------------------------------
            auto workers = co_await qb::require<Worker>(ctx, 200ms);
            int  cores   = 0;
            bool seen[8] = {};
            for (auto const w : workers)
                if (w.index() < 8 && !seen[w.index()]) {
                    seen[w.index()] = true;
                    ++cores;
                }
            qb::io::cout() << "[scout] require<Worker> found " << workers.size() << " workers across " << cores
                           << " cores, and it was handed none of their ids\n";
            // A CONDITIONAL print is a real oracle: had discovery missed the other core, this line
            // would never appear and `dev/agent/run-examples.py` would report a dead @expect path.
            if (workers.size() == 4 && cores == 2)
                qb::io::cout() << "[scout] VERDICT discovery crossed the core boundary: 4 workers on 2 cores\n";

            auto caches = co_await qb::require<Cache>(ctx, 200ms);
            qb::io::cout() << "[scout] require<Cache> found " << caches.size()
                           << " — discovery is BY TYPE, so a Worker never answers for a Cache\n";

            // ---- is that particular one still there? --------------------------------------
            qb::ActorId local, remote;
            for (auto const w : workers) {
                if (w.index() == ctx.id().index() && !local.is_valid())
                    local = w;
                if (w.index() != ctx.id().index() && !remote.is_valid())
                    remote = w;
            }
            const bool alive_local  = co_await qb::ping(ctx, local, 200ms);
            const bool alive_remote = co_await qb::ping(ctx, remote, 200ms);
            // An id nobody ever had: a real message goes out, nothing comes back, and the wait
            // ends at the timeout rather than hanging.
            const bool alive_none = co_await qb::ping(ctx, qb::ActorId{}, 100ms);
            qb::io::cout() << "[scout] ping(local worker)=" << alive_local << " ping(remote worker)=" << alive_remote
                           << " ping(invalid id)=" << alive_none << " — all three round trips\n";
            if (alive_local && alive_remote && !alive_none)
                qb::io::cout() << "[scout] VERDICT ping answered for both live workers and timed out on the invalid id\n";

            // ---- and the same question asked the cheap way --------------------------------
            ctx.push_to<Compare>(ctx.id(), local, remote); // back into actor context
        });
        co_return true;
    }

    void
    on(Compare const &e) {
        // Both of these actors are alive. Only one of them is on this core, and that is the
        // entire difference between the two lines this program prints.
        qb::io::cout() << "[scout] is_actor_alive(local)=" << is_actor_alive(e.local) << " is_actor_alive(remote)=" << is_actor_alive(e.remote)
                       << " — the remote worker is ALIVE and this says otherwise, because it is "
                          "a lookup in THIS core's map\n";
        if (is_actor_alive(e.local) && !is_actor_alive(e.remote))
            qb::io::cout() << "[scout] VERDICT is_actor_alive: true for the LOCAL worker, FALSE for the live remote\n";
        push<StartLegacy>(_legacy);
    }
};

// ---------------------------------------------------------------------------
// The legacy scout: fire-and-forget discovery, replies counted by hand.
// ---------------------------------------------------------------------------
class LegacyScout : public qb::Actor {
    int _workers = 0;
    int _caches  = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<StartLegacy>(*this);
        registerEvent<LegacyReport>(*this);
        // Re-registering RequireEvent binds the handler to THIS type, so the override below runs
        // instead of the kernel's default. That is the mechanism — and the obligation that comes
        // with it is the `resolve_require` call in it.
        registerEvent<qb::RequireEvent>(*this);
        co_return true;
    }

    void
    on(StartLegacy const &) {
        require<Worker, Cache>(); // broadcasts one typed PingEvent per type, and returns at once
        // Fire-and-forget means nothing tells you when the replies have stopped, so a window has
        // to be invented. The awaited form already owns this window; that is the difference.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(200ms);
            ctx.push_to<LegacyReport>(ctx.id());
        });
    }

    void
    on(qb::RequireEvent &e) {
        if (resolve_require(e))
            return; // a reply to a co_await ping/require of OURS — hand it back to that coroutine
        if (is<Worker>(e))
            ++_workers;
        else if (is<Cache>(e))
            ++_caches;
    }

    void
    on(LegacyReport const &) {
        qb::io::cout() << "[legacy] require<Worker, Cache>() replies: " << _workers << " workers, " << _caches
                       << " caches — same answer, a hand-rolled window, and one place to forget "
                          "resolve_require()\n";
        qb::io::cout() << "=== discovery complete: two questions, and only one of them crosses a core ===\n";
        qb::Main::stop();
    }
};

int
main() {
    qb::Main engine;

    engine.addActor<Worker>(0);
    engine.addActor<Worker>(0);
    engine.addActor<Worker>(1);
    engine.addActor<Worker>(1);
    engine.addActor<Cache>(1);

    auto legacy = engine.addActor<LegacyScout>(0);
    engine.addActor<Scout>(0, legacy);

    qb::io::cout() << "[main] 4 workers and 1 cache across 2 cores; nobody is told where\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
