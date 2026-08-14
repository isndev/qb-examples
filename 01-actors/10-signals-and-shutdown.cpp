/**
 * @file examples/01-actors/10-signals-and-shutdown.cpp
 * @tier 01-actors
 * @teaches The first thing anyone needs in order to ship a server: SIGINT and SIGTERM arriving
 *          as ordinary events, an extra signal asked for by name and handled WITHOUT dying,
 *          draining in-flight work before the process leaves — and the exit-code contract,
 *          including the measured reason `hasError()` alone is not a health check.
 * @demonstrates qb::SignalEvent, qb::Main::registerSignal, qb::Main::unregisterSignal,
 *               qb::Main::ignoreSignal, qb::Main::stop, hasError, registerEvent<E>,
 *               kill(), spawn, qb::ScopedCoroContext, ctx.sleep, ctx.push<Drained>,
 *               push<E>, qb::Main, addActor<T>
 * @prerequisites 01-actors/05-lifecycle
 * @expect "[server] accepting; 3 units of work queued"
 * @expect "[server] SIGHUP: reloaded configuration, still serving — a signal is an EVENT"
 * @expect "[server] SIGTERM: stop accepting, drain "
 * @expect "[server] drained; every accepted unit finished before the process leaves"
 * @expect "[main] engine.hasError() = no — cores started, no actor failed its SYNCHRONOUS init"
 * @expect "[main] no application startup failure: nothing refused to bind"
 * @expect "=== shutdown complete, exit code 0 ==="
 *
 * SIGNALS ARE EVENTS HERE
 * -----------------------
 * `qb::Main::start()` installs handlers for **SIGINT and SIGTERM** on its own. The handler
 * itself does almost nothing — it stores the signal number and bumps a generation counter,
 * which is all a signal handler may safely do — and each `VirtualCore` notices the bump on its
 * next pass and BROADCASTS a `qb::SignalEvent{ signum }` to every actor it owns. So a signal
 * arrives on your actor's own thread, in order with its other mail, with no re-entrancy.
 *
 * The default handler, `qb::Actor::on(SignalEvent const &)`, calls `kill()` for SIGINT and
 * SIGTERM and does nothing for anything else. To do something different you re-register:
 *
 *     registerEvent<qb::SignalEvent>(*this);      // <- makes YOUR on(SignalEvent const&) run
 *
 * That line is not optional and its absence is silent. Dispatch is by SUBSCRIPTION, not by
 * vtable: `qb::Actor`'s constructor already subscribed `SignalEvent` with the base as handler,
 * and declaring your own `on(SignalEvent const &)` merely HIDES a name nothing calls — the base
 * keeps running and your actor still dies. `on(SignalEvent const &)` is not virtual, so
 * `override` on it does not even compile. Re-registering is the mechanism.
 *
 * ASKING FOR MORE SIGNALS
 * -----------------------
 *   `qb::Main::registerSignal(n)`    deliver signal n as a `SignalEvent`
 *   `qb::Main::unregisterSignal(n)`  put n back to SIG_DFL
 *   `qb::Main::ignoreSignal(n)`      set n to SIG_IGN — the right answer for SIGPIPE
 *
 * All three are static and may be called before `start()`. A signal that is not SIGINT or
 * SIGTERM is delivered and is **not terminal**: SIGHUP below reloads and keeps serving.
 *
 * THE EXIT-CODE CONTRACT, AND WHY hasError() IS ONLY HALF OF IT
 * -------------------------------------------------------------
 * A server that never bound its port must not `return 0`. `qb::Main::hasError()` reports
 * ENGINE failures — a core that could not start, a core with zero actors, an actor whose
 * SYNCHRONOUS `onInit()` returned false at startup, an escaped exception. It is necessary and
 * it is not sufficient, for two measured reasons:
 *   * an actor whose **async** `onInit()` fails after the engine is running is simply removed
 *     (`VirtualCore.cpp:619`) and sets no engine flag at all;
 *   * "the port was already in use" is an application fact the engine has no opinion about.
 * So a real `main()` reads both: `engine.hasError()` and its own startup-failure flag. This
 * program carries both, and `--fail-bind` makes the second one fire so you can watch the exit
 * code change from 0 to 1 without editing anything.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-signals-and-shutdown
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-signals-and-shutdown
 *   ./build/presets/release/examples/01-actors/qb-example-actors-signals-and-shutdown --fail-bind
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <string_view>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>

using namespace std::chrono_literals;

// The application's own failure channel, read by main() alongside engine.hasError(). An
// `atomic` because it is written on a worker thread and read on the main one after `join()`.
static std::atomic<int> g_startup_failures{0};
static bool             g_fail_bind = false;

struct Unit : qb::Event {
    int n{0};

    explicit Unit(int value)
        : n(value) {}
};

struct Drained : qb::Event {};

// ---------------------------------------------------------------------------------------
// The server: accepts work, reloads on SIGHUP, drains on SIGTERM.
// ---------------------------------------------------------------------------------------
class Server : public qb::Actor {
    int  _in_flight = 0;
    int  _done      = 0;
    int  _reloads   = 0;
    bool _accepting = true;

public:
    qb::io::async::task<bool>
    onInit() override {
        // THE line. Without it the base handler runs and this actor dies on SIGHUP too.
        registerEvent<qb::SignalEvent>(*this);
        registerEvent<Unit>(*this);
        registerEvent<Drained>(*this); // self-addressed; an unregistered event is silently dropped

        // The "bind". A real one would be `transport().listen_v4(port)` (tier 02-io); what
        // matters at this tier is the SHAPE: a failure here must reach main(), and the engine
        // will not carry it for you.
        if (g_fail_bind) {
            g_startup_failures.fetch_add(1, std::memory_order_relaxed);
            qb::io::cout() << "[server] BIND FAILED — recording a startup failure and stopping the engine. "
                              "A server that never bound must not exit 0\n";
            qb::Main::stop();
            co_return true; // the actor is fine; the APPLICATION is not
        }

        for (int i = 1; i <= 3; ++i) {
            push<Unit>(id(), i);
            ++_in_flight;
        }
        qb::io::cout() << "[server] accepting; 3 units of work queued\n";
        co_return true;
    }

    // Each unit takes real time, so a shutdown that did not drain would visibly lose work.
    void
    on(Unit &u) {
        const auto n = u.n;
        spawn([n](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(300ms);
            qb::io::cout() << "[server]   unit " << n << " completed its work\n";
            ctx.push<Drained>(); // bounce back: a coroutine may not touch actor state
        });
    }

    void
    on(Drained &) {
        ++_done;
        --_in_flight;
        qb::io::cout() << "[server] unit finished (" << _done << "/3)\n";
        if (!_accepting && _in_flight == 0)
            finish();
    }

    // `const &`, and NOT `override`: the base declaration is not virtual, and this handler runs
    // because `registerEvent<qb::SignalEvent>` re-pointed the subscription at this type.
    void
    on(qb::SignalEvent const &e) {
#ifdef SIGHUP
        if (e.signum == SIGHUP) {
            ++_reloads;
            qb::io::cout() << "[server] SIGHUP: reloaded configuration, still serving — a signal is an EVENT, and "
                              "only SIGINT/SIGTERM are terminal by default (reload #"
                           << _reloads << ")\n";
            return;
        }
#endif
        if (e.signum == SIGINT || e.signum == SIGTERM) {
            _accepting = false;
            qb::io::cout() << "[server] SIGTERM: stop accepting, drain " << _in_flight << " in flight, then exit\n";
            if (_in_flight == 0)
                finish();
            return;
        }
        qb::io::cout() << "[server] signal " << e.signum << " observed and ignored\n";
    }

private:
    void
    finish() {
        qb::io::cout() << "[server] drained; every accepted unit finished before the process leaves\n";
        kill();
        // `Main::stop()` is static and safe to call from a signal handler — it only stores a
        // pending signal. Here it is simply the programmatic equivalent of SIGTERM.
        qb::Main::stop();
    }
};

// Raises the signals, so the program demonstrates itself rather than waiting for a human with
// a terminal. `std::raise` is the portable spelling; on POSIX qb installs its handlers with
// `sigaction` (SA_RESTART, no SA_RESETHAND) and on Windows with `std::signal`, and both
// intercept a raise from any thread.
class Ticker : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(80ms);
#ifdef SIGHUP
            std::raise(SIGHUP);
            co_await ctx.sleep(80ms);
#endif
            std::raise(SIGTERM);
        });
        co_return true;
    }
};

int
main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view{argv[i]} == "--fail-bind")
            g_fail_bind = true;

    // Before start(), and static. SIGINT and SIGTERM are installed by start() itself; these two
    // are the additions a real service wants.
#ifdef SIGHUP
    qb::Main::registerSignal(SIGHUP); // reload, deliberately non-terminal
#endif
#ifdef SIGPIPE
    // A write to a closed socket must not kill the process. `ignoreSignal` is SIG_IGN, which is
    // what every server wants and what nothing installs for you.
    qb::Main::ignoreSignal(SIGPIPE);
#endif

    qb::Main engine;
    engine.addActor<Server>(0);
    if (!g_fail_bind)
        engine.addActor<Ticker>(0);

    qb::io::cout() << "=== signals and shutdown: SIGHUP reloads, SIGTERM drains, and the exit code means something ===\n\n";

    engine.start();
    engine.join();

    // A signal registration is process-wide state; putting SIGHUP back to its default is the
    // symmetric thing to do, and it is what `unregisterSignal` is for.
#ifdef SIGHUP
    qb::Main::unregisterSignal(SIGHUP);
#endif

    const bool engine_bad = engine.hasError();
    const int  app_bad    = g_startup_failures.load(std::memory_order_relaxed);
    qb::io::cout() << (engine_bad ? "\n[main] engine.hasError() = YES — an engine-level failure\n"
                                  : "\n[main] engine.hasError() = no — cores started, no actor failed its SYNCHRONOUS init\n");
    qb::io::cout() << (app_bad > 0 ? "[main] APPLICATION startup failure recorded: something refused to bind, and the\n"
                                     "       engine has no opinion about that at all\n"
                                   : "[main] no application startup failure: nothing refused to bind\n");

    const int code = (engine_bad || app_bad > 0) ? 1 : 0;
    // Two literals, one per outcome, so the example runner asserts the CONTRACT rather than
    // asserting that the last line was reached.
    qb::io::cout() << (code == 0 ? "=== shutdown complete, exit code 0 ===\n" : "=== shutdown FAILED, exit code 1 ===\n");
    return code;
}
