/**
 * @file examples/03-coroutines/14-foreign-awaitables.cpp
 * @tier 03-coroutines
 * @teaches Awaiting something qb does not own: a raw socket handle, via
 *          `wait_readable`/`wait_writable`/`wait_for_io`, and a callback-based library, via
 *          `async_awaiter<T>`. These two are the escape hatch that keeps a foreign API from
 *          forcing a blocking call onto the event loop.
 * @demonstrates qb::io::async::wait_readable, qb::io::async::wait_writable,
 *               qb::io::async::wait_for_io, qb::io::async::async_awaiter<int>,
 *               qb::io::async::coroutine_scope,
 *               qb::io::udp::socket, native_handle, local_endpoint,
 *               qb::io::async::callback, qb::io::async::task<void>,
 *               qb::io::async::task<int>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 02-io/01-event-loop, 03-coroutines/07-structured-concurrency
 * @expect "[wait_readable] the coroutine parked on a handle qb has never seen"
 * @expect "[wait_readable] and the loop kept turning while it was parked"
 * @expect "[wait_writable] an idle socket is writable straight away"
 * @expect "[wait_for_io] EV_READ | EV_WRITE resumes on whichever comes first"
 * @expect "[async_awaiter] a callback-based API became one co_await"
 * @expect "[async_awaiter] and it works for an operation that completes LATER"
 * @expect "=== foreign awaitables complete: 1 datagram awaited, 2 callbacks bridged ==="
 *
 * THE PROBLEM
 * -----------
 * Sooner or later you hold something the framework knows nothing about: a descriptor from a
 * third-party library, a device handle, a client that only offers `void fetch(callback)`. The
 * wrong answer is to block — `read(fd, …)` or `future.get()` on the loop thread stops every
 * other coroutine, every timer and every socket on this core. The right answer is one of two
 * adapters, and this file is both of them.
 *
 *   wait_readable(h) / wait_writable(h) / wait_for_io(h, events)
 *       Hand the loop a HANDLE and suspend. The coroutine resumes when the handle is ready and
 *       you then do the read or write yourself, non-blocking, in your own code. qb never owns
 *       the handle, never buffers for it and never closes it — this is strictly "tell me when".
 *
 *   async_awaiter<T>(op)
 *       Hand it a function that takes a completion callback. It suspends the coroutine, calls
 *       `op(cb)`, and resumes with whatever `cb` was given. That is the general bridge from
 *       ANY callback-shaped API to `co_await`, and it is what qbm-http's own coroutine client
 *       is built out of.
 *
 * PORTABILITY, DELIBERATELY. The socket below is a `qb::io::udp::socket` bound to an ephemeral
 * loopback port, not a `pipe()` — because `native_handle()` is an `int` on POSIX and a `SOCKET`
 * on Windows, and each of the three wait_* functions ships an overload for both. Writing this
 * with POSIX calls would have made the example not compile on a platform qb supports. UDP
 * rather than TCP so there is no listener, no accept and no fixed port to collide with.
 *
 * ONE-SHOT, LEVEL-TRIGGERED. Each of these awaiters starts a watcher on suspend and stops it on
 * resume. It is not a subscription: to wait again, await again. That is why the loop below is a
 * `while` around a fresh `co_await wait_readable(...)` and not a callback you register once.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-foreign-awaitables
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-foreign-awaitables
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/udp/socket.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// What the summary is gated on: one datagram actually awaited and received, and two callback
// bridges resolved (one immediate, one deferred).
struct Bridged {
    int datagrams = 0;
    int callbacks = 0;
};
using Log = std::shared_ptr<Bridged>;

/**
 * The other half of the demonstration: a coroutine that keeps counting while the reader is
 * parked. If `wait_readable` blocked the thread this would print nothing, which is the whole
 * difference between "wait" and "block".
 */
task<void>
heartbeat(std::shared_ptr<int> beats, int count) {
    for (int i = 0; i < count; ++i) {
        co_await sleep(15ms);
        ++(*beats);
    }
}

/// Sends one datagram to `peer` after a delay, using a socket of its own.
task<void>
send_after(qb::io::endpoint peer, qb::duration delay) {
    co_await sleep(delay);
    qb::io::udp::socket sender;
    sender.init();
    const char payload[] = "one datagram";
    sender.write(payload, sizeof(payload) - 1, peer);
    sender.close();
}

// ---------------------------------------------------------------------------------------
// 1. wait_readable / wait_writable / wait_for_io — the loop as a readiness oracle.
// ---------------------------------------------------------------------------------------
task<void>
demo_raw_handle(Log log) {
    qb::io::cout() << "-- 1. awaiting a handle the framework does not own\n";

    // A plain UDP socket. qb::io::udp::socket is used for its portable `native_handle()`, and
    // for nothing else: from here on it is just a number that libev can watch.
    qb::io::udp::socket sock;
    sock.init();
    if (sock.bind_v4(0, "127.0.0.1") != 0) {
        qb::io::cout() << "    UNEXPECTED: could not bind a loopback UDP socket\n";
        co_return;
    }
    sock.set_nonblocking(true);
    const auto self = sock.local_endpoint();

    // An idle socket has room in its send buffer, so writability is already true and the
    // awaiter resumes on the very next turn of the loop rather than waiting for an event.
    const auto tw = std::chrono::steady_clock::now();
    co_await wait_writable(sock.native_handle());
    const auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tw).count();
    if (write_ms < 20)
        qb::io::cout() << "[wait_writable] an idle socket is writable straight away, so this cost no waiting "
                          "at all — readiness, not a timer\n";
    else
        qb::io::cout() << "[wait_writable] UNEXPECTED: writability took " << write_ms << " ms\n";

    // Now the interesting direction. Nothing has been sent yet, so this parks. A heartbeat
    // coroutine runs alongside to prove the loop is still turning.
    auto            beats = std::make_shared<int>(0);
    coroutine_scope scope;
    scope.spawn(heartbeat(beats, 6));
    scope.spawn(send_after(self, 60ms));

    const auto t0 = std::chrono::steady_clock::now();
    co_await wait_readable(sock.native_handle());
    const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // The awaiter says "ready". Reading is still YOUR job — qb has no buffer for this handle.
    char             buf[64] = {};
    qb::io::endpoint from;
    const int        n = sock.read(buf, sizeof(buf) - 1, from);
    if (n > 0) {
        ++log->datagrams;
        qb::io::cout() << "[wait_readable] the coroutine parked on a handle qb has never seen and woke when the "
                       << "datagram landed: " << n << " bytes, \"" << buf << "\", after " << wait_ms << " ms\n";
    } else {
        qb::io::cout() << "[wait_readable] UNEXPECTED: read returned " << n << "\n";
    }

    if (*beats >= 3)
        qb::io::cout() << "[wait_readable] and the loop kept turning while it was parked — the heartbeat coroutine "
                       << "got " << *beats << " ticks in, which a blocking read would have made zero\n";
    else
        qb::io::cout() << "[wait_readable] UNEXPECTED: only " << *beats << " heartbeat ticks while parked\n";

    // `wait_for_io` is the same awaiter with both flags: it resumes on whichever condition
    // arrives first, and it is then on you to work out which one it was. Here the socket is
    // idle-but-writable, so it is the write side that resolves it immediately.
    const auto t2 = std::chrono::steady_clock::now();
    co_await wait_for_io(sock.native_handle(), EV_READ | EV_WRITE);
    const auto both_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t2).count();
    qb::io::cout() << "[wait_for_io] EV_READ | EV_WRITE resumes on whichever comes first — here the write side, in " << both_ms
                   << " ms, because a drained socket is always writable\n";

    co_await scope.join_all();
    sock.close();
    qb::io::cout() << "\n";
}

// ---------------------------------------------------------------------------------------
// 2. async_awaiter — the bridge from a callback-shaped API.
// ---------------------------------------------------------------------------------------

/// Stand-in for a third-party client: it hands you a result through a callback, and there is
/// no awaitable anywhere in its interface.
void
legacy_fetch_now(std::function<void(int)> done) {
    done(42);
}

/// The harder shape: the callback fires LATER, from the event loop, long after the call
/// returned. `qb::io::async::callback` is qb's own timer, standing in for whatever mechanism
/// the foreign library uses to finish.
void
legacy_fetch_later(std::function<void(int)> done, qb::duration after) {
    qb::io::async::callback([done]() mutable { done(99); }, after);
}

task<void>
demo_callback_bridge(Log log) {
    qb::io::cout() << "-- 2. bridging a callback-based API into co_await\n";

    // The awaiter is constructed as a temporary and awaited in place: `awaiter_base` is
    // deliberately non-copyable and non-movable, and a `co_await` of a prvalue materialises it
    // exactly once, for exactly as long as the suspension lasts.
    const int now = co_await async_awaiter<int>([](std::function<void(int)> complete) { legacy_fetch_now(complete); });
    if (now == 42) {
        ++log->callbacks;
        qb::io::cout() << "[async_awaiter] a callback-based API became one co_await: the operation completed "
                          "synchronously inside await_suspend and the value came straight back\n";
    } else {
        qb::io::cout() << "[async_awaiter] UNEXPECTED: got " << now << "\n";
    }

    const auto t0    = std::chrono::steady_clock::now();
    const int  later = co_await async_awaiter<int>([](std::function<void(int)> complete) { legacy_fetch_later(complete, 40ms); });
    const auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    if (later == 99 && ms >= 30) {
        ++log->callbacks;
        qb::io::cout() << "[async_awaiter] and it works for an operation that completes LATER: the frame stayed "
                       << "parked for " << ms << " ms and resumed from inside the callback\n";
    } else {
        qb::io::cout() << "[async_awaiter] UNEXPECTED: got " << later << " after " << ms << " ms\n";
    }

    // THE ONE RULE. The callback must be invoked EXACTLY once. Never calling it parks the frame
    // forever; calling it twice would resume a frame that has already moved on. The awaiter
    // protects the second case with a liveness flag — it no-ops if the frame is already gone —
    // but it cannot invent a completion nobody delivered.
    qb::io::cout() << "    the contract is exactly one call to `complete`: none parks the frame forever, and "
                      "a second one is ignored rather than resuming a frame that moved on\n\n";
}

task<void>
run_all(Log log) {
    co_await demo_raw_handle(log);
    co_await demo_callback_bridge(log);
}

int
main() {
    qb::io::cout() << "=== foreign awaitables: a raw handle and a callback, both awaited ===\n\n";

    init();

    auto log = std::make_shared<Bridged>();
    run_sync(run_all(log));

    qb::io::cout() << "    measured: " << log->datagrams << " datagram awaited on a raw handle, " << log->callbacks
                   << " callback-based operations bridged\n";
    if (log->datagrams == 1 && log->callbacks == 2)
        qb::io::cout() << "=== foreign awaitables complete: 1 datagram awaited, 2 callbacks bridged ===\n";
    else
        qb::io::cout() << "=== foreign awaitables INCONCLUSIVE: the bridge counts are not 1 and 2 ===\n";
    return 0;
}
