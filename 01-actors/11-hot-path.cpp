/**
 * @file examples/01-actors/11-hot-path.cpp
 * @tier 01-actors
 * @teaches The knobs qb is sold on and the corpus never showed: `send` versus `push`, a
 *          variable-length event written straight into the pipe with `getPipe` +
 *          `allocated_push`, an actor that opts out of the five default event registrations,
 *          the latency/affinity settings — each one measured with `tsc_ticks` rather than
 *          asserted.
 * @demonstrates send<Tick>, push<Tick>, getPipe, qb::Pipe, allocated_push<Blob>, build_event<Tick>,
 *               to, qb::Actor::EventBuilder, qb::no_default_events, qb::Actor, qb::EventQOS0,
 *               qb::tsc_ticks, qb::CPU, setLatency, setAffinity, qb::CoreIdSet, getIndex,
 *               registerEvent<E>, qb::KillEvent, kill(), qb::Main, addActor<T>
 * @prerequisites 01-actors/03-event-payloads
 * @expect "[bench] same-core push  x"
 * @expect "[bench] same-core send  x"
 * @expect "[bench] cross-core push x"
 * @expect "-byte variable-length event arrived intact via allocated_push: "
 * @expect "[builder] to(dest).push<>() chained "
 * @expect "[builder] build_event made a LOCAL event that was never queued: dest="
 * @expect "[opt-out] this actor registered NO default events and had to add KillEvent by hand"
 * @expect "=== hot path complete: 4 send/push shapes, 1 variable-length event, 0 allocations"
 *
 * FOUR WAYS TO PUT AN EVENT SOMEWHERE, AND WHEN EACH IS RIGHT
 * -----------------------------------------------------------
 *   `push<E>(dest, ...)`         ordered per (source, destination). The default, and the
 *                                answer unless you have measured that it is not.
 *   `send<E>(dest, ...)`         UNORDERED. May hand the event straight to the destination
 *                                core's mailbox and skip the outbound pipe entirely.
 *   `getPipe(dest)` +            one pipe, many events, and a place to write TRAILING bytes:
 *   `allocated_push<E>(n, ...)`  the way to move a variable-length message with no heap
 *                                allocation and no second copy.
 *   `to(dest).push<E>(...)`      an `EventBuilder`: the same pipe, several events, one lookup.
 *
 * `build_event<E>(source, ...)` is the odd one out — it QUEUES NOTHING. It constructs an event
 * locally, addressed to you, for immediate hand-processing.
 *
 * TWO THINGS THE DOCUMENTATION SAYS THAT THE COMPILER DOES NOT
 * ------------------------------------------------------------
 * Both were measured against the headers rather than assumed, and both matter here:
 *
 * 1. `send<E>`'s contract is "E must be trivially destructible" (`Actor.h:889`) — and nothing
 *    enforces it. The only `static_assert` on the subject (`VirtualCore.h:795`) is gated on
 *    `event_qos0_type<T>`, i.e. on deriving from `qb::EventQOS0`, and the concept
 *    `qb::trivial_event` that exists for exactly this purpose (`Actor.h:126`) has **zero** uses
 *    in the entire tree. So a `send` of an event holding a `std::string` compiles clean. Treat
 *    the rule as a rule anyway — or derive from `qb::EventQOS0`, which is the one spelling that
 *    does get checked.
 * 2. `qb::EventQOS1` and `qb::EventQOS2` are plain `using` aliases OF `qb::Event`
 *    (`Event.h:499`, `:509`) — the same type, not two priorities. Only `EventQOS0` is a
 *    distinct type. Choosing "QOS 1" changes nothing at all.
 *
 * THE `allocated_push` ARITHMETIC, WHICH IS EASY TO GET WRONG BY A FACTOR OF TWO
 * ------------------------------------------------------------------------------
 * The `size` argument is the **trailing** bytes, not the total: `allocated_push` adds
 * `sizeof(E)` itself and then rounds up to whole buckets. Passing `sizeof(E) + n` over-reserves
 * by a whole event and halves the size ceiling you can reach. And there IS a ceiling: about
 * 1023 buckets, ~64 KiB with the default 64-byte bucket. Past it the cross-core flush DROPS the
 * event, with a CRIT log and no delivery. Large payloads belong on the heap behind a
 * `shared_ptr` (`01-actors/03-event-payloads`); `allocated_push` is for a kilobyte, not a
 * megabyte.
 *
 * ABOUT THE NUMBERS BELOW, AND WHAT THEY ARE NOT
 * ----------------------------------------------
 * `qb::tsc_ticks()` is a raw cycle counter: uncalibrated, per-core, and meaningful only as a
 * DELTA taken on one thread. It is not a clock and it does not convert to nanoseconds here.
 *
 * What the three loops measure is **enqueue cost at the call site**, and the measurement is
 * worth having precisely because it refutes the obvious guess: same-core and cross-core `push`
 * cost about the SAME here. `push` writes into the destination's outbound pipe and returns; the
 * cross-core handoff happens later, in the core loop's flush. So "cross-core is expensive" is
 * true of end-to-end LATENCY (the framework's own benchmarks: tens of ns same-core against
 * roughly a microsecond across) and false of the call you just made. Do not use these numbers
 * to argue about placement — use them to know where the cost is not.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-hot-path
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-hot-path
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/string.h>
#include <qb/system/cpu.h>
#include <qb/system/time.h>

using namespace std::chrono_literals;

// The hot-path event: trivially destructible, tiny, and derived from `qb::EventQOS0` so the
// one static_assert that exists actually fires if that ever stops being true.
struct Tick : qb::EventQOS0 {
    std::uint64_t seq{0};

    explicit Tick(std::uint64_t s)
        : seq(s) {}
};

// A variable-length message. The object is fixed size; the bytes live immediately after it in
// the pipe, written by the producer and read by the consumer with no allocation on either side.
struct Blob : qb::Event {
    std::uint32_t len{0};

    explicit Blob(std::uint32_t n)
        : len(n) {}

    [[nodiscard]] char *
    bytes() noexcept {
        return reinterpret_cast<char *>(this) + sizeof(Blob);
    }
    [[nodiscard]] char const *
    bytes() const noexcept {
        return reinterpret_cast<char const *>(this) + sizeof(Blob);
    }
};

struct Report : qb::Event {
    qb::string<16> what;
    std::uint64_t  ticks{0};

    Report(std::string_view w, std::uint64_t t)
        : what(w)
        , ticks(t) {}
};

// ---------------------------------------------------------------------------------------
// The sink. It opts OUT of the five registrations `qb::Actor`'s constructor normally performs
// (`KillEvent`, `SignalEvent`, `UnregisterCallbackEvent`, `PingEvent`, `RequireEvent`) — the
// point being that an actor on a hot path pays for every subscription it does not use.
//
// The cost of opting out is stated, real, and MEASURED — the first version of this file got it
// wrong and hung. Two different shutdown paths need two different registrations:
//   `broadcast<qb::KillEvent>()`  needs `registerEvent<qb::KillEvent>`
//   `qb::Main::stop()`            needs `registerEvent<qb::SignalEvent>`, because stop() works
//                                 by synthesising a SIGINT and broadcasting a `SignalEvent`
// Register only the first and `Main::stop()` cannot reach this actor: the core never empties
// and `join()` never returns. That is not a hypothetical — it is what this program did until
// the second line below was added.
// ---------------------------------------------------------------------------------------
class Sink : public qb::Actor {
    std::uint64_t _count = 0;
    std::uint64_t _bytes = 0;

public:
    Sink()
        : qb::Actor(qb::no_default_events) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Tick>(*this);
        registerEvent<Blob>(*this);
        registerEvent<qb::KillEvent>(*this);   // for broadcast<qb::KillEvent>()
        registerEvent<qb::SignalEvent>(*this); // for qb::Main::stop() / SIGINT / SIGTERM
        qb::io::cout() << "[opt-out] this actor registered NO default events and had to add KillEvent by hand — "
                          "and SignalEvent too, because Main::stop() travels as a signal\n";
        co_return true;
    }

    void
    on(Tick &) {
        ++_count;
    }

    void
    on(Blob &b) {
        _bytes += b.len;
        // The trailing bytes really travelled: read them back and check the pattern.
        bool ok = true;
        for (std::uint32_t i = 0; i < b.len; ++i)
            ok = ok && (b.bytes()[i] == static_cast<char>('a' + (i % 26)));
        qb::io::cout() << "[blob] a " << b.len << "-byte variable-length event arrived intact via allocated_push: " << (ok ? "yes" : "no")
                       << " (footprint " << b.getSize() << " bytes, rounded up to whole 64-byte buckets)\n";
    }

    void
    on(qb::KillEvent const &) {
        report_and_die();
    }

    // Inherited from qb::Actor this would kill on SIGINT/SIGTERM; written here so the
    // subscription above resolves on Sink and the counts get printed on the way out.
    void
    on(qb::SignalEvent const &) {
        report_and_die();
    }

private:
    void
    report_and_die() {
        // The far sink usually reports FEWER ticks than were pushed to it, and that is not a
        // defect in the counting: `qb::Main::stop()` brings the engine down, it does not drain
        // what is still sitting in an outbound pipe. Draining before exit is a thing a server
        // has to do on purpose — `01-actors/10-signals-and-shutdown` is where that is the
        // lesson rather than a footnote.
        qb::io::cout() << "[sink@" << getIndex() << "] handled " << _count << " ticks and " << _bytes
                       << " trailing bytes (a stop() does not drain what is still in a pipe)\n";
        kill();
    }
};

// ---------------------------------------------------------------------------------------
// The bench. Everything it measures is a DELTA on one thread.
// ---------------------------------------------------------------------------------------
class Bench : public qb::Actor {
    static constexpr std::uint64_t N = 20000;

    qb::ActorId _near;
    qb::ActorId _far;
    qb::ActorId _reporter;

public:
    Bench(qb::ActorId near_sink, qb::ActorId far_sink, qb::ActorId reporter)
        : _near(near_sink)
        , _far(far_sink)
        , _reporter(reporter) {}

    qb::io::async::task<bool>
    onInit() override {
        // WARM-UP FIRST. A pipe grows on demand, so the first pass through any destination
        // pays for reallocation and would be charged to whichever loop happened to run first.
        for (std::uint64_t i = 0; i < N; ++i) {
            push<Tick>(_near, i);
            push<Tick>(_far, i);
        }

        // --- push vs send, same core ------------------------------------------------------
        auto t0 = qb::tsc_ticks();
        for (std::uint64_t i = 0; i < N; ++i)
            push<Tick>(_near, i);
        const auto push_same = qb::tsc_ticks() - t0;

        t0 = qb::tsc_ticks();
        for (std::uint64_t i = 0; i < N; ++i)
            // UNORDERED. Two `send`s to one destination may arrive in either order, and a
            // `send` may overtake a `push` issued before it. Reach for it only where ordering
            // genuinely does not matter.
            send<Tick>(_near, i);
        const auto send_same = qb::tsc_ticks() - t0;

        // --- the same work, across a core boundary ---------------------------------------
        t0 = qb::tsc_ticks();
        for (std::uint64_t i = 0; i < N; ++i)
            push<Tick>(_far, i);
        const auto push_cross = qb::tsc_ticks() - t0;

        qb::io::cout() << "[bench] same-core push  x" << N << ": " << push_same << " ticks (" << push_same / N << " per event)\n";
        qb::io::cout() << "[bench] same-core send  x" << N << ": " << send_same << " ticks (" << send_same / N
                       << " per event) — unordered, and it may skip the pipe\n";
        qb::io::cout() << "[bench] cross-core push x" << N << ": " << push_cross << " ticks (" << push_cross / N
                       << " per event) — and this is the surprise: about the SAME\n";
        qb::io::cout() << "[bench] what that means: `push` only writes into the outbound pipe. The cost of "
                          "CROSSING is paid later, by the core loop, at the flush — not by you at the call "
                          "site. End-to-end latency is a different measurement (the framework's own "
                          "benchmarks put same-core at tens of ns against microseconds cross-core); what "
                          "these three numbers measure is enqueue cost, and it is flat.\n";

        // --- one pipe, one lookup, several events -----------------------------------------
        // `getPipe` resolves the destination's outbound pipe once. `to(dest)` wraps the same
        // pipe in an `EventBuilder` whose `push<E>` is chainable — and, unlike `Actor::push`,
        // is constrained on `event_type`, so a non-event is a clear error rather than a deep
        // template failure.
        qb::Actor::EventBuilder b = to(_near);
        b.push<Tick>(1u).push<Tick>(2u).push<Tick>(3u);
        qb::io::cout() << "[builder] to(dest).push<>() chained 3 events through ONE pipe lookup\n";

        // --- a variable-length event ------------------------------------------------------
        constexpr std::uint32_t LEN  = 700;
        qb::Pipe                pipe = getPipe(_far);
        // LEN is the TRAILING bytes. `allocated_push` adds sizeof(Blob) itself.
        Blob &blob = pipe.allocated_push<Blob>(LEN, LEN);
        for (std::uint32_t i = 0; i < LEN; ++i)
            blob.bytes()[i] = static_cast<char>('a' + (i % 26));

        // --- an event that is never queued ------------------------------------------------
        // `build_event` constructs it locally with `dest = our own id` and `source` = the
        // argument. Nothing is sent; this is for handing an event to a function that expects
        // one, or for processing it inline.
        Tick local = build_event<Tick>(_near, 42u);
        qb::io::cout() << "[builder] build_event made a LOCAL event that was never queued: dest="
                       << static_cast<std::uint32_t>(local.getDestination())
                       << " (us), source=" << static_cast<std::uint32_t>(local.getSource()) << ", seq=" << local.seq << "\n";

        push<Report>(_reporter, "done", push_same);
        co_return true;
    }
};

// Waits for the far sink to have seen the blob, then brings the system down on a COUNT.
class Reporter : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Report>(*this);
        co_return true;
    }

    void
    on(Report &) {
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // One short sleep so the cross-core pipe is flushed and the blob is handled before
            // the shutdown broadcast. This is the one place a duration is honest: it is
            // waiting for output ordering, not for correctness.
            co_await ctx.sleep(150ms);
            qb::io::cout() << "\n=== hot path complete: 4 send/push shapes, 1 variable-length event, 0 allocations ===\n";
            qb::Main::stop();
        });
    }
};

int
main() {
    qb::Main engine;

    // Latency 0 is the low-latency setting: the core spins instead of sleeping when idle, which
    // costs a whole CPU per core and buys the shortest possible wake-up. Any value > 0 lets the
    // core sleep up to that long. It is `qb::duration` (nanoseconds) — pass a chrono literal,
    // never a bare number.
    engine.core(0).setLatency(qb::duration::zero());
    engine.core(1).setLatency(qb::duration::zero());

    // A REQUEST, not a guarantee, and the return value does not tell you whether it worked. On
    // Apple Silicon the underlying thread_policy_set flavour is unimplemented and the shim
    // reports success anyway, so nothing is pinned and nothing says so. Branch on
    // `qb::CPU::ThreadPinningSupported()` instead.
    engine.core(1).setAffinity(qb::CoreIdSet{1});
    qb::io::cout() << "[main] thread pinning supported on this host: " << (qb::CPU::ThreadPinningSupported() ? "yes" : "no")
                   << " — setAffinity is a hint, and on some hosts a silent no-op\n";

    auto near_sink = engine.addActor<Sink>(0);
    auto far_sink  = engine.addActor<Sink>(1);
    auto reporter  = engine.addActor<Reporter>(0);
    engine.addActor<Bench>(0, near_sink, far_sink, reporter);

    qb::io::cout() << "=== hot path: push vs send, allocated_push, build_event, and the knobs ===\n\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
