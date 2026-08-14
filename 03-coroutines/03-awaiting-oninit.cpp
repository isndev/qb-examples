/**
 * @file examples/03-coroutines/03-awaiting-oninit.cpp
 * @tier 03-coroutines
 * @teaches 3.0's headline behavioural change — `onInit()` is a coroutine, so setup that WAITS
 *          is written straight down the page. What the engine does with an actor whose init is
 *          still in flight (it stashes its mail instead of dropping it), how to wait for one
 *          without guessing a sleep, and the deadline that fails a stalled init rather than
 *          wedging a core forever.
 * @demonstrates qb::io::async::task<bool>, context(), ctx.sleep, is_alive, is_active,
 *               addRefActor<Database>, qb::ActorHandle<Database>, id(), get(), ready_async,
 *               is_actor_alive, qb::VirtualCore::activation_deadline_ns, spawn,
 *               qb::ScopedCoroContext, registerEvent<E>, push<Ping>, qb::Main
 * @prerequisites 03-coroutines/02-actor-coroutines
 * @expect "nothing has suspended yet, so this actor still looks Active"
 * @expect "[client] two pings pushed BEFORE the DB was ready; handle.get() is nullptr"
 * @expect "NOW it is Activating, its mail is being stashed, "
 * @expect "[db] connected; onInit returns true"
 * @expect "(it was stashed during activation, not dropped)"
 * @expect "[client] Actor::time() before the core's first loop pass reads "
 * @expect "[client] ready_async said "
 * @expect "[deadline] ready_async on a stalled child gave up and returned "
 * @expect "[deadline] and the engine failed the stalled init on its own: is_actor_alive = "
 * @expect "=== awaiting onInit complete: mail stashed, handle resolved, stall reaped ==="
 *
 * THE CHANGE
 * ----------
 * Before 3.0 an actor's `onInit()` returned `bool` and could not wait for anything. Setup that
 * needed a database, a socket or a peer had to be split across an event handler and a state
 * flag, and every message that arrived before the flag flipped had to be queued by hand — or
 * was silently dropped. In 3.0 `onInit()` returns `qb::io::async::task<bool>` and may
 * `co_await`, and the engine takes over the queueing.
 *
 * THE THIRD STATE, WHICH IS THE WHOLE LESSON
 * ------------------------------------------
 * An actor is no longer just alive or dead. While its `onInit()` is SUSPENDED it is
 * **Activating**, and the two predicates say different things about it:
 *
 *     is_alive()   true   — the object exists, it can be killed, it owns its coroutine scope
 *     is_active()  false  — it is not yet serving
 *
 * "Suspended" is load-bearing and this program measures it: before the first `co_await`,
 * `is_active()` still reads **true**, because the engine clears `_activated` only once the
 * init frame has actually parked. An actor is not born Activating; it becomes Activating at
 * its first suspension — which is why the synchronous majority, whose `onInit` never
 * suspends, never observes `false` at all.
 *
 * Everything else follows from that one distinction:
 *   * inbound unicast events are STASHED (FIFO, cap 4096) and replayed in order on activation
 *     — not dropped, and not delivered early;
 *   * `ActorHandle::id()` is usable immediately, because an id needs no object;
 *   * `ActorHandle::get()` returns **nullptr**, because a pointer would let you call into an
 *     actor that has not finished setting itself up;
 *   * `is_actor_alive(id)` also returns false — it is `is_active()`, not `is_alive()`;
 *   * `getService<T>()` is the one lookup that is NOT gated, deliberately, so a service can
 *     resolve a peer from inside its own `onInit` (`01-actors/07-service-actor`);
 *   * three things bypass the stash so an init cannot deadlock on its own mail: broadcasts,
 *     any `KillEvent` — an Activating actor stays killable — and the reply to a `qb::ask` the
 *     actor issued from inside its own `onInit`.
 *
 * HOW TO WAIT FOR ONE, AND HOW NOT TO
 * -----------------------------------
 * `handle.ready_async(context())` is an awaitable that resolves to `true` once the child is
 * active, or `false` on its timeout (5 s by default). It is a 1 ms poll on `ready()`, and it
 * is cancellation-aware, so killing the WAITING actor unwinds it. The thing it replaces is a
 * guessed `co_await ctx.sleep(100ms)`, which is a correctness bug wearing a duration.
 *
 * AND THE DEADLINE
 * ----------------
 * An `onInit()` that never completes would hold its stash and its slot forever. The engine
 * bounds it: `qb::VirtualCore::activation_deadline_ns`, **5 s by default**, settable before
 * `qb::Main::start()`. On expiry the actor's coroutine scope is cancelled, the frame unwinds
 * with `cancelled_error`, the never-replayed stash is disposed properly (payload destructors
 * run) and the actor is removed. `main()` below lowers it to 800 ms so this program can show
 * that happening in about a second instead of five.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-awaiting-oninit
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-awaiting-oninit
 */

#include <chrono>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>

using namespace std::chrono_literals;

// A constructor rather than an aggregate, because `push<Ping>(dest, 1)` direct-initialises the
// event in place — `T(args...)` — and parenthesised aggregate initialisation (C++20's P0960)
// is not available on every toolchain this corpus builds with. One explicit ctor per event is
// the corpus convention for that reason.
struct Ping : qb::Event {
    int n{0};

    explicit Ping(int value)
        : n(value) {}
};

// ---------------------------------------------------------------------------------------
// A dependency whose setup really takes time. `co_await` here is the point: what used to be a
// handshake state machine spread over three handlers is four lines read top to bottom.
// ---------------------------------------------------------------------------------------
class Database : public qb::Actor {
    qb::duration _connect_cost;

public:
    explicit Database(qb::duration connect_cost)
        : _connect_cost(connect_cost) {}

    qb::io::async::task<bool>
    onInit() override {
        // Register BEFORE suspending. The stash is replayed after activation, so a late
        // registration would still work — but a reader should not have to know that.
        registerEvent<Ping>(*this);

        // MEASURED, AND NOT WHAT THE PHASE TABLE SUGGESTS. Before the first `co_await` this
        // actor still reports `is_active() == true`, because `_activated` starts true and the
        // engine only clears it once the init frame has actually SUSPENDED
        // (`VirtualCore.cpp:524`, reached from `__drive_init__` after the resume returns
        // "still running"). "Activating" is therefore a state an actor enters at its first
        // suspension, not one it is born in — which is exactly right for the synchronous
        // majority, whose `onInit` never suspends and never sees `false`.
        qb::io::cout() << "[db] onInit begins — is_alive() " << (is_alive() ? "yes" : "no") << ", is_active() " << (is_active() ? "yes" : "no")
                       << ": nothing has suspended yet, so this actor still looks Active\n";

        // The "connect". `context()` hands out the same ScopedCoroContext `spawn` would, so
        // this sleep is cancellation-aware: a kill during init unwinds it.
        co_await context().sleep(_connect_cost);

        qb::io::cout() << "[db] after the first suspension — is_alive() " << (is_alive() ? "yes" : "no") << ", is_active() "
                       << (is_active() ? "yes" : "no") << ": NOW it is Activating, its mail is being stashed, "
                       << "and handles refuse to resolve it\n";
        qb::io::cout() << "[db] connected; onInit returns true — the engine will flip is_active() and replay "
                          "whatever arrived meanwhile\n";
        co_return true;
    }

    void
    on(Ping &p) {
        qb::io::cout() << "[db] served ping " << p.n << " (it was stashed during activation, not dropped); is_active() now "
                       << (is_active() ? "yes" : "no") << "\n";
    }
};

// An `onInit()` that never finishes. Nothing is wrong with its code — it is waiting for
// something that will not happen, which is what a mutual-init deadlock looks like from the
// inside. The engine's deadline is what turns it into a removal instead of a wedged core.
class StalledDatabase : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        co_await context().sleep(60s);
        co_return true;
    }
};

// ---------------------------------------------------------------------------------------
// The client: its OWN onInit awaits, and it waits for a child rather than guessing.
// ---------------------------------------------------------------------------------------
class Client : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        // A child on this same core. The handle is usable the instant it returns.
        qb::ActorHandle<Database> db = addRefActor<Database>(qb::duration{200ms});

        // `id()` needs no object, so mail can be addressed immediately — and it is stashed,
        // not dropped. `get()` needs an ACTIVE actor, so it is null for now.
        push<Ping>(db.id(), 1);
        push<Ping>(db.id(), 2);
        qb::io::cout() << "[client] two pings pushed BEFORE the DB was ready; handle.get() is nullptr: " << (db.get() == nullptr ? "yes" : "no")
                       << ", but handle.id() already routes\n";

        // ONE CLOCK TRAP, MEASURED. `Actor::time()` is the VirtualCore's CACHED loop clock,
        // refreshed once per loop pass (`VirtualCore.cpp:644`) — and its initial value is 0
        // (`VirtualCore.h:377`). At engine start `onInit` runs BEFORE the core's first pass,
        // so it reads 0 here and an elapsed-time subtraction against it yields the whole UNIX
        // epoch. It is exactly right everywhere the loop is already turning, which is every
        // other use of it in this corpus; for a startup measurement use a real clock.
        qb::io::cout() << "[client] Actor::time() before the core's first loop pass reads " << time()
                       << " ns — the cached loop clock has not been set yet, so time a startup wait with "
                          "steady_clock instead\n";

        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = co_await db.ready_async(context());
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        qb::io::cout() << "[client] ready_async said " << (ok ? "yes" : "no")
                       << "; handle.get() now resolves: " << (db.get() != nullptr ? "yes" : "no") << " (waited " << ms
                       << " ms — exactly as long as it took, rather than a guessed sleep)\n";

        co_return true;
    }
};

// ---------------------------------------------------------------------------------------
// The deadline, demonstrated. Runs from a spawned coroutine rather than from an `onInit`, so
// this actor is not itself subject to the (lowered) activation deadline while it waits.
// ---------------------------------------------------------------------------------------
class Conductor : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        spawn([this](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // `this` is captured for ONE use, before the first suspension, and never touched
            // afterwards — the rest of the body talks only to `ctx` and to locals. Capturing
            // an actor pointer to use AFTER a co_await is the corpus's sharpest footgun.
            auto stalled = this->addRefActor<StalledDatabase>();
            auto bad_id  = stalled.id();

            const auto t0 = ctx.time();
            const bool ok = co_await stalled.ready_async(ctx, 300ms);
            qb::io::cout() << "\n[deadline] ready_async on a stalled child gave up and returned " << (ok ? "true" : "false") << " after "
                           << (ctx.time() - t0) / 1'000'000 << " ms — that is OUR patience, not the engine's\n";

            // Now wait past the engine's own activation deadline (lowered to 800 ms in main).
            co_await ctx.sleep(700ms);

            // `is_actor_alive` is `is_active()`, same-core only. The stalled child was
            // cancelled, its frame unwound, its stash was disposed and it was removed.
            const bool alive = this->is_actor_alive(bad_id);
            qb::io::cout() << "[deadline] and the engine failed the stalled init on its own: is_actor_alive = " << (alive ? "yes" : "no")
                           << " (deadline was " << qb::VirtualCore::activation_deadline_ns / 1'000'000 << " ms)\n";

            qb::io::cout() << "\n=== awaiting onInit complete: mail stashed, handle resolved, stall reaped ===\n";
            qb::Main::stop();
        });
        co_return true;
    }
};

int
main() {
    // Lowered so the stall is reaped in about a second instead of five. This is a real knob,
    // documented for exactly this use ("set it before start(), e.g. lower in tests"), and 0
    // would disable the guard entirely — which is how a mutual-init deadlock becomes a hang.
    qb::VirtualCore::activation_deadline_ns = 800ull * 1000ull * 1000ull;

    qb::Main engine;
    engine.addActor<Client>(0);
    engine.addActor<Conductor>(0);

    qb::io::cout() << "=== an onInit that really awaits: Activating, the stash, and the deadline ===\n\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
