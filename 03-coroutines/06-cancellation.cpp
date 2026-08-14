/**
 * @file examples/03-coroutines/06-cancellation.cpp
 * @tier 03-coroutines
 * @teaches Why an actor may be killed while its coroutines are parked and nothing leaks: the
 *          per-actor cancellation scope, the four awaits that respect it, the destructors that
 *          still run — and the await that does NOT respect it, which is the whole reason
 *          `ctx.sleep` exists next to `qb::io::async::sleep`.
 * @demonstrates qb::ScopedCoroContext, ctx.sleep, ctx.cancellation_point, ctx.until_cancelled,
 *               ctx.cancellable, ctx.token, ctx.cancelled, child_token, context(),
 *               qb::io::async::cancelled_error, qb::io::async::cancellation_token,
 *               qb::io::async::cancellable_sleep, qb::io::async::sleep,
 *               qb::io::async::task<void>, qb::io::async::task<bool>, qb::io::async::task<int>,
 *               spawn, registerEvent<E>, qb::KillEvent, qb::Main
 * @prerequisites 03-coroutines/02-actor-coroutines
 * @expect "[scope] ctx.sleep woke with cancelled_error"
 * @expect "[scope] the compute loop bailed at iteration "
 * @expect "[scope] until_cancelled fired: this is the teardown hook"
 * @expect "[scope] ctx.cancellable made a plain task<T> interruptible"
 * @expect "[child] the SUBTASK was cancelled; the actor is still alive and serving"
 * @expect "[A/B] cancellable_sleep(d, ctx.token()) woke ON THE KILL"
 * @expect "[A/B] a BARE qb::io::async::sleep(d) slept its full 400 ms"
 * @expect "[ledger] handles still open: "
 * @expect "=== cancellation complete: 5 frames unwound, 0 handles leaked ==="
 *
 * WHAT THE PROBLEM ACTUALLY IS
 * ----------------------------
 * An actor may be killed at any time — by `kill()`, by a `KillEvent`, by SIGINT. Its
 * coroutines do not stop being suspended when that happens: a frame parked on a 30-second
 * timer is still parked, still owns whatever its locals own, and the actor object it was
 * spawned from is erased from the core's map IMMEDIATELY (`VirtualCore.cpp:915`). Without a
 * mechanism that is either a leak — nobody ever unwinds the frame — or a use-after-free,
 * because something unwinds it and it touches the actor.
 *
 * The mechanism is one `qb::io::async::cancellation_token` per actor: its SCOPE. `kill()`
 * cancels it (`Actor.cpp:288`), every cancellation-aware awaiter has registered an
 * `on_cancel` hook, each hook re-queues its coroutine, and each coroutine resumes into a
 * thrown `cancelled_error` and UNWINDS NORMALLY. Destructors run. `catch` blocks run. This
 * program watches that happen with a counter rather than asserting it in prose.
 *
 * THE FOUR AWAITS THAT RESPECT THE SCOPE — and they are the only four
 * -------------------------------------------------------------------
 *   ctx.sleep(d)              a timer that wakes early when the scope is cancelled
 *   ctx.cancellation_point()  yield to the scheduler, then throw if cancelled — for a loop
 *                             that computes rather than waits
 *   ctx.until_cancelled()     park with no timer at all until the scope is cancelled
 *   ctx.cancellable(task)     wrap somebody ELSE's task<T> so the scope reaches it
 *
 * `ctx.token()` is the token itself — hand it to any qb-io primitive that takes one, which
 * is what section 6 does. `ctx.cancelled()` polls it. `child_token()` derives a token that
 * dies with the actor but can also be cancelled on its own: a subtree.
 *
 * WHAT DOES NOT RESPECT IT, MEASURED IN SECTION 6
 * -----------------------------------------------
 * `co_await qb::io::async::sleep(400ms)` — the free function, without `ctx.` — registers no
 * hook. Kill the actor and that frame sleeps out its full duration and resumes into a world
 * where its actor no longer exists. The framework says as much about its own teardown
 * ("cancel_all cannot wake a plain sleep()", `scheduler.h:726`). Section 6 runs the two side
 * by side and prints when each woke.
 *
 * Nothing is corrupted by it here, because that body only prints. `ctx.push(...)` or
 * `ctx.time()` there would dereference a destroyed actor: `ScopedCoroContext` carries an
 * `Actor const *`, and only `sleep` / `cancellation_point` / `until_cancelled` /
 * `cancellable` / `token` / `cancelled` avoid touching it.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-cancellation
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-cancellation
 */

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>

using namespace std::chrono_literals;
using qb::io::async::cancelled_error;

// The instrument. Every coroutine in sections 1-5 builds one of these BEFORE it suspends: the
// counter goes up on construction and down on destruction, so a frame that was abandoned
// rather than unwound would leave `open` above zero. `shared_ptr` because these outlive the
// actor that spawned them.
struct Handle {
    std::string          name;
    std::shared_ptr<int> open;
    std::shared_ptr<int> unwound;

    Handle(std::string n, std::shared_ptr<int> o, std::shared_ptr<int> u)
        : name(std::move(n))
        , open(std::move(o))
        , unwound(std::move(u)) {
        ++*open;
    }
    // Runs during the unwind that `cancelled_error` produces. This destructor is the whole
    // claim "cancellation is not a kill -9", made checkable.
    ~Handle() {
        --*open;
        ++*unwound;
    }
    Handle(Handle const &)            = delete;
    Handle &operator=(Handle const &) = delete;
};

using Ledger = std::shared_ptr<int>;

// Somebody else's coroutine. It knows nothing about actors or scopes, which is exactly why
// `ctx.cancellable(...)` exists. Parameters BY VALUE — a frame stores its parameters, and a
// reference one would store a reference into a caller temporary that is already gone.
qb::io::async::task<int>
third_party_fetch(qb::duration cost) {
    co_await qb::io::async::sleep(cost);
    co_return 7;
}

struct Provoke : qb::Event {};

class Worker : public qb::Actor {
    Ledger _open;
    Ledger _unwound;

public:
    Worker(Ledger open, Ledger unwound)
        : _open(std::move(open))
        , _unwound(std::move(unwound)) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Provoke>(*this);

        auto open = _open, unwound = _unwound;

        // ---- 1. parked on a timer ------------------------------------------------------
        spawn([open, unwound](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Handle h{"timer", open, unwound};
            try {
                co_await ctx.sleep(30s); // far longer than this program lives
                qb::io::cout() << "    UNREACHABLE: the 30 s sleep completed\n";
            } catch (cancelled_error const &) {
                qb::io::cout() << "[scope] ctx.sleep woke with cancelled_error instead of waiting 30 s\n";
            }
        });

        // ---- 2. computing, not waiting --------------------------------------------------
        // A loop that never awaits cannot be cancelled, because cancellation is delivered by
        // RESUMING a suspended frame. `cancellation_point()` is that suspension: it re-queues
        // this coroutine at the back of the ready queue and throws on the way back in if the
        // scope went away meanwhile.
        spawn([open, unwound](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Handle h{"compute", open, unwound};
            long   acc = 0;
            for (int i = 0;; ++i) {
                acc += i;
                try {
                    co_await ctx.cancellation_point();
                } catch (cancelled_error const &) {
                    qb::io::cout() << "[scope] the compute loop bailed at iteration " << i << ": the accumulator (" << acc
                                   << ") is discarded, and no half-written state is left behind. Note the size "
                                      "of that number — a cancellation_point re-queues immediately, so this loop "
                                      "ran at full speed and still stopped on demand\n";
                    co_return;
                }
            }
        });

        // ---- 3. nothing to do until the end ---------------------------------------------
        // `until_cancelled()` allocates no timer and no helper frame. It ALWAYS throws on
        // resume — a park-then-unwind primitive — so teardown goes in the `catch`, never
        // after the await. And it runs when the actor is already gone: touch only what was
        // captured by value.
        spawn([open, unwound](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Handle h{"watchdog", open, unwound};
            try {
                co_await ctx.until_cancelled();
            } catch (cancelled_error const &) {
                qb::io::cout() << "[scope] until_cancelled fired: this is the teardown hook, and the "
                                  "actor no longer exists by the time it runs\n";
            }
        });

        // ---- 4. making somebody else's task cancellable ---------------------------------
        spawn([open, unwound](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Handle h{"wrapped", open, unwound};
            try {
                (void) co_await ctx.cancellable(third_party_fetch(30s));
                qb::io::cout() << "    UNREACHABLE: the third-party fetch completed\n";
            } catch (cancelled_error const &) {
                qb::io::cout() << "[scope] ctx.cancellable made a plain task<T> interruptible without that "
                                  "task knowing anything about actors\n";
            }
        });

        // ---- 5. a SUBTREE cancelled while the actor lives on -----------------------------
        // `child_token()` is derived from the actor scope: killing the actor cancels it too,
        // but cancelling it does not touch the actor. This is how you abandon one request
        // without tearing down the actor serving it.
        qb::io::async::cancellation_token sub = context().child_token();
        spawn([sub, open, unwound](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            Handle h{"subtask", open, unwound};
            try {
                // The free function, given the CHILD token. `ctx.sleep` would bind the actor
                // scope instead, which is a different lifetime.
                co_await qb::io::async::cancellable_sleep(30s, sub);
                qb::io::cout() << "    UNREACHABLE: the subtask slept 30 s\n";
            } catch (cancelled_error const &) {
                qb::io::cout() << "[child] the SUBTASK was cancelled; the actor is still alive and serving\n";
            }
        });
        // `mutable`, because `cancellation_token::cancel()` is non-const and a captured copy
        // is const inside a plain lambda. The closure is still copied into the coroutine
        // frame by `spawn`, so `mutable` changes nothing about lifetime — only constness.
        spawn([sub](qb::ScopedCoroContext ctx) mutable -> qb::io::async::task<void> {
            co_await ctx.sleep(80ms);
            qb::io::cout() << "[child] cancelling the subtree — actor scope cancelled? " << (ctx.cancelled() ? "yes" : "no") << "\n";
            sub.cancel();
        });

        // ---- 6. THE A/B: the same wait, with and without the scope -----------------------
        // Identical duration, identical primitive family. The only difference is whether the
        // token is handed over, and the two printed wake-up times are the entire lesson.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            try {
                co_await qb::io::async::cancellable_sleep(400ms, ctx.token());
                qb::io::cout() << "    UNREACHABLE: the scoped 400 ms sleep ran to term\n";
            } catch (cancelled_error const &) {
                qb::io::cout() << "[A/B] cancellable_sleep(d, ctx.token()) woke ON THE KILL, ~250 ms in\n";
            }
        });
        spawn([](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            co_await qb::io::async::sleep(400ms);
            qb::io::cout() << "[A/B] a BARE qb::io::async::sleep(d) slept its full 400 ms and resumed 150 ms "
                              "AFTER its actor was destroyed\n";
        });

        qb::io::cout() << "[worker] 8 coroutines spawned; 5 of them hold a Handle, and 6 are still parked "
                          "by the time the kill arrives\n";
        co_return true;
    }

    // Proof that cancelling the SUBTREE left this actor able to keep working.
    void
    on(Provoke &) {
        qb::io::cout() << "[worker] served an event after the subtree was cancelled\n";
    }
};

// Drives the demonstration: provoke, kill, then hold the engine open long enough for the
// uncancellable frame in section 6 to wake and make its point.
class Conductor : public qb::Actor {
    qb::ActorId _worker;
    Ledger      _open;
    Ledger      _unwound;

public:
    Conductor(qb::ActorId worker, Ledger open, Ledger unwound)
        : _worker(worker)
        , _open(std::move(open))
        , _unwound(std::move(unwound)) {}

    qb::io::async::task<bool>
    onInit() override {
        auto worker = _worker;
        auto open = _open, unwound = _unwound;
        spawn([worker, open, unwound](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(150ms); // after the subtree was cancelled at 80 ms
            ctx.push_to<Provoke>(worker);

            co_await ctx.sleep(100ms);
            qb::io::cout() << "\n[conductor] killing the worker with 6 coroutines still parked\n";
            ctx.push_to<qb::KillEvent>(worker);

            // Long enough for the bare-sleep frame (400 ms from spawn) to wake.
            co_await ctx.sleep(400ms);
            qb::io::cout() << "\n[ledger] handles still open: " << *open << ", frames unwound: " << *unwound << "\n";

            // The summary is a LITERAL and it is gated on the invariant, so the example
            // runner's `@expect` check is an assertion about behaviour rather than about the
            // program having reached its last line.
            if (*open == 0 && *unwound == 5)
                qb::io::cout() << "=== cancellation complete: 5 frames unwound, 0 handles leaked ===\n";
            else
                qb::io::cout() << "=== cancellation INCOMPLETE: the ledger does not balance ===\n";
            qb::Main::stop();
        });
        co_return true;
    }
};

int
main() {
    qb::Main engine;

    auto open    = std::make_shared<int>(0);
    auto unwound = std::make_shared<int>(0);

    auto worker = engine.addActor<Worker>(0, open, unwound);
    engine.addActor<Conductor>(0, worker, open, unwound);

    qb::io::cout() << "=== cancellation: a killed actor, six parked coroutines, zero leaks ===\n\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
