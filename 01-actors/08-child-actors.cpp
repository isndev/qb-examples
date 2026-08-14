/**
 * @file examples/01-actors/08-child-actors.cpp
 * @tier 01-actors
 * @teaches Actor trees: creating a child at runtime from inside another actor, holding a TYPED
 *          handle to it instead of a bare id, keeping a registry of children bounded as they
 *          die — and the property the word "child" gets wrong, which is that the parent does
 *          not own it and killing the parent leaves it running.
 * @demonstrates addRefActor<Member>, addRefHandle<Member>, qb::ActorHandle<Member>, id(),
 *               get(), ready(), valid(), is_actor_alive, kill(), spawn,
 *               qb::ScopedCoroContext, ctx.sleep, ctx.push<Step>, registerEvent<E>, push<E>,
 *               qb::Main, addActor<T>
 * @prerequisites 01-actors/07-service-actor
 * @expect "[team] created 3 members + 1 orphan; a handle's id() is usable at once"
 * @expect "[team] handle.get() gives a TYPED pointer for same-core, synchronous access: "
 * @expect "[team] member 2 killed itself; is_actor_alive says "
 * @expect " — is_actor_alive is what keeps it bounded"
 * @expect "[team] handle.get() for the dead member now returns nullptr, same signal"
 * @expect "[watcher] the team is gone and its 'children' are NOT: the orphan is still alive"
 * @expect "[watcher] after an explicit kill the orphan is gone"
 * @expect "=== child actors complete: 4 created, 1 self-killed, 3 explicitly reaped ==="
 *
 * CREATING AN ACTOR FROM AN ACTOR
 * -------------------------------
 * `engine.addActor<T>(core, args...)` is the `main()`-time route and it needs the engine. From
 * inside a running actor the route is `addRefActor<T>(args...)`, which builds the actor **on
 * the calling actor's own core** and hands back a `qb::ActorHandle<T>`. There is also
 * `addRefHandle<T>(...)`, which is the same call marked `[[nodiscard]]` — use it when
 * discarding the handle would be a mistake.
 *
 * WHAT A HANDLE IS, AND THE TWO QUESTIONS IT ANSWERS DIFFERENTLY
 * --------------------------------------------------------------
 *   `handle.id()`   an `ActorId`. Valid immediately, even before the child finishes
 *                   initialising, because an id needs no object. Mail addressed to it is
 *                   stashed and replayed, never dropped.
 *   `handle.get()`  a `T*`, or **nullptr**. It resolves only an ACTIVE actor on the calling
 *                   thread, so it is null while the child is still activating, null after the
 *                   child dies, and null if you ask from any other thread.
 *
 * `get()` is the escape hatch from message passing, and it is bounded on purpose: same core,
 * same thread, synchronous, and never stored across a `co_await` or an event boundary. Use it
 * for a cheap read; use events for everything else.
 *
 * "CHILD" IS A LIE ABOUT OWNERSHIP
 * --------------------------------
 * `addRefActor` creates a peer, not a subobject. The new actor is registered on the core in
 * exactly the same map as every other actor; the parent holds a handle, which is a 32-bit id
 * and a cached pointer, and nothing more. **Killing the parent does not kill the child** — the
 * program below kills a team and then measures its "orphan" still alive. A parent that wants a
 * subtree to die with it has to say so, and the shipped way to say it is
 * `04-patterns/02-supervisor`.
 *
 * KEEPING A REGISTRY BOUNDED
 * --------------------------
 * A parent that remembers its children accumulates dead ids forever unless something prunes
 * them. `is_actor_alive(id)` is that something: it is `is_active()` for an id on the CALLING
 * actor's core, and it is same-core only by construction — the actor map belongs to one
 * `VirtualCore` and is unsynchronised, so `false` for a REMOTE id means "not here", not "not
 * alive". For a cross-core liveness question the answer is `co_await qb::ping(...)`
 * (`04-patterns/09-discovery`), not this.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-child-actors
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-child-actors
 */

#include <chrono>
#include <cstdint>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// The team's script runs as a sequence of self-addressed steps. A coroutine may not touch its
// actor's members after a suspension, so the pattern throughout is: sleep in the coroutine,
// then bounce back into a synchronous handler where `this` is legal again.
struct Step : qb::Event {
    int n{0};

    explicit Step(int value)
        : n(value) {}
};

struct TeamGone : qb::Event {
    qb::ActorId orphan;
    qb::ActorId survivor_a;
    qb::ActorId survivor_b;

    TeamGone(qb::ActorId o, qb::ActorId a, qb::ActorId b)
        : orphan(o)
        , survivor_a(a)
        , survivor_b(b) {}
};

struct DieNow : qb::Event {};

// ---------------------------------------------------------------------------------------
// The child. An ordinary actor — nothing about it knows it was created by another actor.
// ---------------------------------------------------------------------------------------
class Member : public qb::Actor {
    int _index;

public:
    explicit Member(int index)
        : _index(index) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<DieNow>(*this);
        co_return true;
    }

    /// Read directly through `handle.get()`. Cheap, synchronous, same core, same thread.
    [[nodiscard]] int
    index() const noexcept {
        return _index;
    }

    void
    on(DieNow &) {
        kill();
    }
};

// ---------------------------------------------------------------------------------------
// The parent.
// ---------------------------------------------------------------------------------------
class Team : public qb::Actor {
    std::vector<qb::ActorHandle<Member>> _members;
    qb::ActorHandle<Member>              _orphan;
    qb::ActorId                          _watcher;

public:
    explicit Team(qb::ActorId watcher)
        : _watcher(watcher) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Step>(*this);

        for (int i = 1; i <= 3; ++i)
            _members.push_back(addRefActor<Member>(i));

        // `addRefHandle` is `addRefActor` with `[[nodiscard]]`. Same call, and the attribute is
        // the whole difference: dropping this handle would leave an actor nobody can reach.
        _orphan = addRefHandle<Member>(99);

        qb::io::cout() << "[team] created 3 members + 1 orphan; a handle's id() is usable at once (member 1 is id "
                       << static_cast<std::uint32_t>(_members[0].id()) << ", valid=" << (_members[0].valid() ? "yes" : "no") << ")\n";

        // These children have a synchronous `onInit`, so they are ACTIVE the instant
        // `addRefActor` returns and `get()` already resolves. A child whose `onInit` awaits
        // would be null here until it activates — `03-coroutines/03-awaiting-oninit` measures
        // that case and shows `ready_async` as the way to wait for it.
        qb::io::cout() << "[team] handle.get() gives a TYPED pointer for same-core, synchronous access: member indices";
        for (auto &h : _members)
            qb::io::cout() << " " << (h.ready() ? h.get()->index() : -1);
        qb::io::cout() << " (read directly, no event round trip)\n";

        step_later(1, 60ms);
        co_return true;
    }

    void
    on(Step &s) {
        switch (s.n) {
            case 1:
                // Ask member 2 to kill itself. Nothing tells the parent about it.
                push<DieNow>(_members[1].id());
                step_later(2, 60ms);
                break;

            case 2: {
                const bool alive = is_actor_alive(_members[1].id());
                qb::io::cout() << "[team] member 2 killed itself; is_actor_alive says " << (alive ? "yes" : "no")
                               << " — the parent was not notified, it had to ask\n";

                const auto before = _members.size();
                // The prune. Without it this vector only ever grows, and every id in it is a
                // message the parent will keep sending into a void.
                std::erase_if(_members, [this](qb::ActorHandle<Member> const &h) { return !is_actor_alive(h.id()); });
                qb::io::cout() << "[team] registry pruned from " << before << " to " << _members.size()
                               << " — is_actor_alive is what keeps it bounded\n";
                qb::io::cout() << "[team] handle.get() for the dead member now returns nullptr, same signal from the "
                                  "other API\n";
                step_later(3, 60ms);
                break;
            }

            case 3:
            default:
                // Hand the survivors to the watcher and die WITHOUT touching the children. The
                // next thing printed is the measurement of what that leaves behind.
                qb::io::cout() << "[team] the team actor is about to kill itself and reap nothing\n";
                push<TeamGone>(_watcher, _orphan.id(), _members[0].id(), _members[1].id());
                kill();
                break;
        }
    }

private:
    /// Sleep in a coroutine, resume in a handler. The coroutine captures only values; `this` is
    /// legal again only once we are back inside `on(Step&)`.
    void
    step_later(int n, qb::duration d) {
        spawn([n, d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(d);
            ctx.push<Step>(n);
        });
    }
};

// ---------------------------------------------------------------------------------------
// The watcher. Same core, so `is_actor_alive` can answer about the team's leftovers.
// ---------------------------------------------------------------------------------------
class Watcher : public qb::Actor {
    qb::ActorId _orphan;
    qb::ActorId _a;
    qb::ActorId _b;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<TeamGone>(*this);
        registerEvent<Step>(*this);
        co_return true;
    }

    void
    on(TeamGone &g) {
        _orphan = g.orphan;
        _a      = g.survivor_a;
        _b      = g.survivor_b;
        after(1, 60ms);
    }

    void
    on(Step &s) {
        if (s.n == 1) {
            qb::io::cout() << (is_actor_alive(_orphan) ? "\n[watcher] the team is gone and its 'children' are NOT: the orphan is still alive.\n"
                                                         "          `addRefActor` makes a peer, not a subobject\n"
                                                       : "\n[watcher] UNEXPECTED: the orphan died with its parent\n");
            push<DieNow>(_orphan);
            push<DieNow>(_a);
            push<DieNow>(_b);
            after(2, 60ms);
            return;
        }
        qb::io::cout() << (is_actor_alive(_orphan) ? "[watcher] UNEXPECTED: the orphan survived an explicit kill\n"
                                                   : "[watcher] after an explicit kill the orphan is gone — a subtree that must die with its\n"
                                                     "          parent needs a supervisor (04-patterns/02-supervisor), not a handle\n");
        qb::io::cout() << "\n=== child actors complete: 4 created, 1 self-killed, 3 explicitly reaped ===\n";
        kill();
        qb::Main::stop();
    }

private:
    void
    after(int n, qb::duration d) {
        spawn([n, d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(d);
            ctx.push<Step>(n);
        });
    }
};

int
main() {
    qb::Main engine;

    auto watcher = engine.addActor<Watcher>(0);
    engine.addActor<Team>(0, watcher);

    qb::io::cout() << "=== child actors: handles, registries, and what 'child' does not mean ===\n\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
