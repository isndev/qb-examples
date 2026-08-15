/**
 * @file examples/05-services/04-shutdown-and-drain/main.cpp
 * @tier 05-services
 * @teaches The full shutdown story of a real server, end to end: SIGTERM arrives as an event, the
 *          acceptor STOPS ACCEPTING, the work already taken is DRAINED to completion, every output
 *          buffer is FLUSHED to its socket, and only then does the process leave — with an exit
 *          code that means something, including on the path where the port could not be bound.
 * @demonstrates qb::SignalEvent, qb::Main::stop, qb::Main::ignoreSignal, hasError, addActor,
 *               qb::ActorId, qb::io::use<T>::tcp::acceptor, qb::io::use<T>::tcp::io_handler<S>,
 *               transport().listen_v4, local_endpoint, registerSession, sessions, session_count,
 *               has_pending_write, has_active_coroutines, active_coroutine_count, spawn,
 *               qb::ScopedCoroContext, ctx.sleep, ctx.push_to, qb::ICallback, registerCallback,
 *               unregisterCallback, qb::LoopEvent, registerEvent<E>, kill, qb::io::cout
 * @prerequisites 05-services/01-tcp-chat, 03-coroutines/06-cancellation, 01-actors/10-signals-and-shutdown
 * @expect "=== 05-services/04: stop accepting, drain, flush, exit ==="
 * @expect "[accept] listening on 127.0.0.1:"
 * @expect "[accept] SIGTERM: closing the listener. No further connection is accepted."
 * @expect "[pool] draining: work already taken will finish; new requests are refused"
 * @expect "[pool] drained: "
 * @expect "[main] engine.hasError() = no, application startup failures = "
 * @expect "=== shutdown complete, exit code 0 ==="
 *
 * WHAT "GRACEFUL" ACTUALLY MEANS, IN ORDER
 * ----------------------------------------
 *   1. **Stop accepting.** Close the listening socket. A client that connects one millisecond
 *      later gets a refusal it can retry, which is far better than a connection accepted and then
 *      dropped mid-request.
 *   2. **Drain what you took.** Every request already accepted runs to completion. This is the
 *      step everybody skips, and it is the one the user notices.
 *   3. **Flush.** A reply that was computed but is still sitting in an output buffer was not
 *      delivered. `has_pending_write()` is the question; `eos` is the event (`02-io/09`).
 *   4. **Exit, saying what happened.** `return 0` from a server that never bound its port is a lie
 *      a supervisor will believe.
 *
 * NOBODY COORDINATES THIS, AND THAT IS THE DESIGN
 * -----------------------------------------------
 * `qb::Main::start()` installs SIGINT/SIGTERM handlers that do almost nothing — they record the
 * signal — and each `VirtualCore` then BROADCASTS a `qb::SignalEvent` to the actors it owns. So the
 * acceptor, the pool and every client are told independently, on their own threads, in order with
 * their other mail, and each takes its own decision: close the listener, start draining, wait for
 * my answer. No shutdown coordinator, no ordering to get wrong. (The first draft did invent a
 * `BeginDrain` event; `events.h` records why it was deleted.)
 *
 * To act on the signal you must `registerEvent<qb::SignalEvent>(*this)`. Dispatch is by
 * SUBSCRIPTION, not by vtable: declaring `on(SignalEvent const &)` without re-registering hides a
 * name nothing calls, and the base handler kills your actor instead. Not hypothetical — the first
 * version of this file omitted it on the pool, and all four in-flight units were lost with no
 * message saying so. **`qb::Main::stop()` travels the same road**: it arrives as a `SignalEvent`,
 * not a `KillEvent`, so an actor built with `no_default_events_t` that registers only `KillEvent`
 * can never be stopped.
 *
 * THE DRAIN POLL IS AN ICallback, NOT A COROUTINE, AND THAT IS NOT A STYLE CHOICE
 * ------------------------------------------------------------------------------
 * The natural first idea is a coroutine sleeping 20 ms in a loop until the system is quiet. It
 * cannot work, for two independent reasons: a coroutine may only reach the actor system through
 * its `ctx` (`push`, `push_to`, `broadcast`, `id`, `time`) because the actor may be destroyed while
 * it is parked — so it cannot read `active_coroutine_count()` at all — and it would itself be
 * counted BY that number, so the drain would wait for itself. `qb::ICallback` runs on the actor's
 * own thread every turn with full access to actor state; the pool registers it when the drain
 * begins and unregisters it the moment it ends, so a healthy server never pays for it.
 *
 * THE EXIT-CODE CONTRACT, AND THE DEFECT IT CLOSES
 * -----------------------------------------------
 * `hasError()` reports ENGINE failures: a core that would not start, an actor whose SYNCHRONOUS
 * `onInit()` returned false at startup, an escaped exception. It is necessary and NOT sufficient,
 * and the gap is measurable in this very corpus: an actor whose **async** `onInit()` returns false
 * once the engine is running is simply REMOVED, silently, setting no engine flag — so both other
 * servers in this tier keep running with their acceptors gone, holding no port, serving nothing,
 * and exit 0. `dev/agent/example-run.manifest` records that as `known-bad-stays-up`. This program
 * does the other thing: a failed bind records an application startup failure AND asks the engine
 * to stop, and `main()` reads both channels plus the work accounting. `--fail-bind` takes that
 * path on purpose, so the exit code can be watched changing from 0 to 1.
 *
 * Build and run:
 *   cmake --build --preset release --target qb-example-services-shutdown-and-drain
 *   ./build/presets/release/examples/05-services/04-shutdown-and-drain/qb-example-services-shutdown-and-drain
 *   …same binary… --fail-bind        # the non-zero exit path
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <qb/actor.h>
#include <qb/icallback.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/main.h>
#include "events.h"
#include "load.h"
#include "session.h"

using namespace std::chrono_literals;
using namespace drain_demo;

namespace {

/// The application's own failure channel, read by main() ALONGSIDE engine.hasError(). Atomic
/// because it is written on a worker thread and read on the main one after join().
std::atomic<int> g_startup_failures{0};

constexpr int kClientCount = 4;
bool          g_fail_bind  = false;

} // namespace

namespace drain_demo {

// ================================================================================== the pool
//
// The actor that owns every session on this core, the work they ask for, and the drain.

class SessionPool
    : public qb::Actor
    , public qb::ICallback
    , public qb::io::use<SessionPool>::tcp::io_handler<WorkSession> {
    bool _draining  = false;
    int  _accepted  = 0;
    int  _completed = 0;
    int  _refused   = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<NewConnection>(*this);
        registerEvent<WorkRequest>(*this);
        registerEvent<WorkDone>(*this);
        // THE line — see the header block. Its absence cost this program four units of work.
        registerEvent<qb::SignalEvent>(*this);
        co_return true;
    }

    /// A connection arrives from the acceptor's core, with its socket inside the event.
    void
    on(NewConnection &evt) {
        if (_draining) {
            // Not defensive: the acceptor and the pool were told about the signal independently
            // and neither waits for the other, so a connection accepted a moment before the
            // listener closed can legitimately land here. Refusing is the honest answer.
            ++_refused;
            return;
        }
        registerSession(std::move(evt.socket));
        ++_accepted;
        qb::io::cout() << "[pool] accepted connection " << _accepted << " (" << session_count() << " session(s) live)\n";
    }

    /// A session decoded a request. The WORK happens here, on the pool's thread, in a coroutine.
    void
    on(WorkRequest &evt) {
        if (_draining) {
            ++_refused;
            auto session = sessions().find(evt.session);
            if (session != sessions().end())
                *session->second << "refused: the service is shutting down" << WorkSession::Protocol::end;
            return;
        }

        // The unit of work: real elapsed time, so a shutdown that did NOT drain would visibly lose
        // it. Everything the coroutine needs is captured BY VALUE before the first co_await — the
        // actor may be gone by the time it resumes — and it talks back only through `ctx`.
        const auto who  = evt.session;
        const auto what = evt.what;
        const auto self = id();
        spawn([who, what, self](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(300ms);
            WorkDone done;
            done.session = who;
            done.answer  = qb::string<64>("done: " + std::string(what.c_str()));
            ctx.push_to<WorkDone>(self, done);
        });
    }

    /// A coroutine finished and bounced its result back. Coroutines never touch actor state.
    void
    on(WorkDone &evt) {
        ++_completed;
        g_work_completed.fetch_add(1, std::memory_order_relaxed);
        auto session = sessions().find(evt.session);
        if (session == sessions().end()) {
            // The connection went away while its work was in flight. Counting it is the point:
            // work that finished with nowhere to go is not the same as work never taken.
            g_work_abandoned.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        *session->second << evt.answer.c_str() << WorkSession::Protocol::end;
    }

    /// SIGTERM, delivered to THIS actor on THIS thread. Begin draining instead of dying.
    void
    on(qb::SignalEvent const &e) {
        if ((e.signum != SIGINT && e.signum != SIGTERM) || _draining)
            return;
        _draining = true;
        qb::io::cout() << "[pool] draining: work already taken will finish; new requests are refused\n";
        qb::io::cout() << "[pool] in flight at the moment the signal arrived: " << active_coroutine_count() << " coroutine(s), "
                       << session_count() << " session(s)\n";
        // The poll starts HERE, not at construction: a healthy server must not spend a callback per
        // loop turn asking whether it is shutting down.
        registerCallback(*this);
    }

    /// The every-turn hook. Steps 2 and 3 of the shutdown live here.
    void
    on(qb::LoopEvent const &) override {
        if (!_draining)
            return;

        // Step 2 — is every unit of work finished? `active_coroutine_count()` is this actor's own
        // in-flight coroutines, readable here because this runs on the actor's thread.
        if (has_active_coroutines())
            return;

        // Step 3 — has every reply actually reached its socket? A message computed and still
        // sitting in an output buffer was not delivered.
        for (auto &[ident, session] : sessions()) {
            if (session->has_pending_write())
                return;
        }

        unregisterCallback(*this); // a drain that finished must stop costing a turn
        qb::io::cout() << "[pool] drained: " << _completed << " unit(s) completed, " << _refused << " refused after the signal, "
                       << session_count() << " session(s) flushed\n";

        // The sessions are owned by this io_handler, so killing the actor takes them with it — and
        // the two checks above are what make that safe rather than abrupt.
        kill();
        qb::Main::stop();
    }
};

// A session hands what it decoded to its OWNER and does nothing else. Defined here because the
// owner's type is necessarily incomplete inside session.h.
void
WorkSession::on(Protocol::message &&msg) {
    auto &evt   = server().push<WorkRequest>(server().id());
    evt.session = id();
    evt.what    = qb::string<32>(msg.text.substr(0, 31));
}

} // namespace drain_demo

namespace {

// ============================================================================== the acceptor

class AcceptActor
    : public qb::Actor
    , public qb::io::use<AcceptActor>::tcp::acceptor {
    const qb::ActorId _pool;
    const qb::ActorId _conductor;
    bool              _accepting = true;

public:
    AcceptActor(qb::ActorId pool, qb::ActorId conductor)
        : _pool(pool)
        , _conductor(conductor) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<qb::SignalEvent>(*this);

        // Port 1 is unbindable without privilege — the failure path, on demand.
        const std::uint16_t port = g_fail_bind ? std::uint16_t{1} : std::uint16_t{0};
        if (transport().listen_v4(port, "127.0.0.1") != 0) {
            // THE DEFECT THIS CLOSES. Returning false here removes the actor and sets NO engine
            // flag — `hasError()` stays false and the rest of the service keeps running, holding
            // no port. Both other servers in this tier do exactly that. So the failure is recorded
            // on the application's own channel AND the engine is asked to stop.
            g_startup_failures.fetch_add(1, std::memory_order_relaxed);
            qb::io::cerr() << "[accept] BIND FAILED on port " << port
                           << " — recording an application startup failure and stopping.\n"
                              "         A server that never bound its port must not exit 0.\n";
            qb::Main::stop();
            _accepting = false; // nothing to close when the stop comes back round as a SignalEvent
            co_return true;
        }

        const auto bound = transport().local_endpoint().port();
        qb::io::cout() << "[accept] listening on 127.0.0.1:" << bound << "\n";
        start();

        // Tell the conductor the EPHEMERAL port that was actually bound. Every actor in this
        // program exists before `start()`, so this id is valid, and an event delivered to an actor
        // whose own `onInit()` has not finished is STASHED and replayed when it activates — which
        // is what makes the startup deterministic with no sleeps and no fixed port.
        push<ServiceUp>(_conductor).port = bound;
        co_return true;
    }

    void
    on(accepted_socket_type &&new_io) {
        if (!_accepting) {
            new_io.close(); // the listener is closed; anything left in the backlog is refused
            return;
        }
        auto &evt  = push<NewConnection>(_pool);
        evt.socket = std::move(new_io);
    }

    void
    on(qb::SignalEvent const &e) {
        if (e.signum != SIGINT && e.signum != SIGTERM)
            return;

        if (_accepting) {
            _accepting = false;
            // STEP 1. Closing the listening socket is what "stop accepting" MEANS. `disconnect()`
            // on the acceptor stops its watcher and closes the fd, so the port is released here
            // rather than at process exit.
            qb::io::cout() << "[accept] SIGTERM: closing the listener. No further connection is accepted.\n";
            this->disconnect();
        }

        // OUTSIDE the branch, and that is not tidiness. Replacing the default `SignalEvent` handler
        // means YOU own the decision to die, on every path — and an early `return` that skips
        // `kill()` leaves this actor alive, its core running and the engine unable to stop. That is
        // exactly what an earlier draft did on the `--fail-bind` path: the bind failed, everything
        // else shut down correctly, and the process then hung forever with one idle actor.
        //
        // Nothing is told to drain: the pool received the same broadcast on its own thread and is
        // already doing it. An acceptor that no longer accepts has no work.
        kill();
    }

    void
    on(qb::io::async::event::disconnected const &) {
        // Reached by our own `disconnect()` above, and by a genuine listener failure. Nothing to
        // do in either case — the shutdown is already under way.
    }
};

} // namespace

int
main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view{argv[i]} == "--fail-bind")
            g_fail_bind = true;

    qb::io::cout() << "=== 05-services/04: stop accepting, drain, flush, exit ===\n";

#ifdef SIGPIPE
    // A write to a socket the peer already closed must not kill the process. Nothing installs this
    // for you, and a shutdown is exactly when it happens.
    qb::Main::ignoreSignal(SIGPIPE);
#endif

    qb::Main engine;

    // One core per role, which is the tier's subject: the acceptor never competes with the work,
    // and the load never competes with either.
    //
    // EVERY actor is created BEFORE start(), and the wiring runs backwards from the leaves so each
    // constructor gets the ids it needs: clients, then the conductor that drives them, then the
    // acceptor that tells the conductor which port it managed to bind. `engine.core(n)` hands back
    // a `CoreInitializer` and THROWS once the engine is running — adding actors after `start()` is
    // not a thing this framework does, and the exception says so out loud.
    const auto pool = engine.core(1).addActor<SessionPool>();

    std::vector<qb::ActorId> clients;
    if (!g_fail_bind) {
        for (int i = 1; i <= kClientCount; ++i)
            clients.push_back(engine.core(2).addActor<Client>(i));
    }
    const auto conductor = engine.core(0).addActor<Conductor>(clients);
    engine.core(0).addActor<AcceptActor>(pool, conductor);

    engine.start(); // non-blocking: the cores are running from here
    engine.join();

    // ------------------------------------------------------------------------- the exit code
    const bool engine_bad = engine.hasError();
    const int  app_bad    = g_startup_failures.load(std::memory_order_relaxed);
    const int  sent       = g_requests_sent.load(std::memory_order_relaxed);
    const int  answered   = g_replies_seen.load(std::memory_order_relaxed);
    const int  completed  = g_work_completed.load(std::memory_order_relaxed);
    const int  abandoned  = g_work_abandoned.load(std::memory_order_relaxed);

    qb::io::cout() << "\n--- what the shutdown actually did ---\n";
    qb::io::cout() << (engine_bad ? "[main] engine.hasError() = YES — an engine-level failure\n"
                                  : "[main] engine.hasError() = no, application startup failures = " + std::to_string(app_bad) + "\n");
    qb::io::cout() << "[main] " << sent << " request(s) sent, " << completed << " completed, " << answered << " answered, " << abandoned
                   << " finished with nowhere to go\n";

    // Two independent failure channels, plus the work accounting. A drain that lost a unit is a
    // failed shutdown even though nothing crashed — which is the whole reason for counting.
    const bool lost = !g_fail_bind && (sent != completed || answered != sent);
    const int  code = (engine_bad || app_bad > 0 || lost) ? 1 : 0;
    if (lost)
        qb::io::cerr() << "[main] WORK WAS LOST: the drain did not finish what it accepted\n";

    qb::io::cout() << (code == 0 ? "=== shutdown complete, exit code 0 ===\n" : "=== shutdown FAILED, exit code 1 ===\n");
    return code;
}
