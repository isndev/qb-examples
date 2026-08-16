/**
 * @file examples/03-coroutines/09-channels.cpp
 * @tier 03-coroutines
 * @teaches Handing values from one coroutine to another through a queue whose CAPACITY is the
 *          backpressure policy, closing it as the one shutdown protocol, and waiting on several
 *          of them at once with `select`.
 * @demonstrates qb::io::async::channel<int>, qb::io::async::channel<std::string>,
 *               qb::io::async::channel_closed, qb::io::async::select,
 *               qb::io::async::select_result, qb::io::async::make_channel,
 *               qb::io::async::transform, qb::io::async::filter, qb::io::async::collect,
 *               qb::io::async::make_pipeline, try_send, try_recv, recv_for, send_for,
 *               is_closed, qb::io::async::coroutine_scope, qb::io::async::task<void>,
 *               qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/07-structured-concurrency
 * @expect "[capacity 3] the producer got 3 sends through and then PARKED"
 * @expect "[capacity 0] a rendezvous channel let 0 sends through"
 * @expect "[close] recv() returned nullopt after the buffer drained"
 * @expect "[close] send() on a closed channel threw channel_closed"
 * @expect "[try_send] returned false on a full channel"
 * @expect "[recv_for] timed out after"
 * @expect "[send_for] returned false"
 * @expect "[select] two channels, different types; the winner was index"
 * @expect "[select] the second wake reported closed=true"
 * @expect "[pipeline] filter -> transform -> collect produced"
 * @expect "=== channels complete: capacity is the policy, close is the protocol ==="
 *
 * WHAT A CHANNEL IS FOR
 * ---------------------
 * Two coroutines, one producing and one consuming, at different speeds. Without a channel you
 * write a vector plus a flag plus a "is there more?" convention and get it subtly wrong. With
 * one you write `co_await ch.send(v)` and `co_await ch.recv()`, and the two questions that
 * matter become explicit:
 *
 *   CAPACITY  is the backpressure policy. `channel<T> ch(3)` lets the producer run three items
 *             ahead of the consumer and then STOPS it. `channel<T> ch(0)` — the default — is a
 *             rendezvous: a send does not complete until a receiver is there to take it. That
 *             is not a degenerate case, it is the strictest and most useful setting, because
 *             it makes a fast producer inherit its consumer's speed instead of a growing queue.
 *
 *   CLOSE     is the shutdown protocol, and it is the ONLY one. `close()` is idempotent, it
 *             drains rather than discards (buffered values still come out), a subsequent
 *             `recv()` returns `std::nullopt` once empty, and a subsequent `send()` throws
 *             `channel_closed` rather than silently dropping the value.
 *
 * SINGLE THREAD. This channel is a same-thread MPSC queue between coroutines on ONE event loop.
 * It is not a way to talk to another core — that is what actor events are for. It is also
 * non-copyable AND non-movable, so it lives where it is declared (or on the heap behind
 * `make_channel`), and every worker takes it by reference.
 *
 * TWO MEASUREMENTS, NOT TWO CLAIMS. §1 and §2 both start a producer with NO consumer running,
 * let the loop turn, and count how many sends completed. A capacity-3 channel answers 3 and a
 * capacity-0 channel answers 0. Those two numbers are what the summary is gated on.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-channels
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-channels
 */

#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// The two measured "how far did the producer get before it was stopped" numbers.
struct Backpressure {
    int buffered_before_park   = -1;
    int rendezvous_before_park = -1;
};
using Notes = std::shared_ptr<Backpressure>;

/**
 * A producer that reports its own progress. `sent` is incremented AFTER the await returns, so
 * it counts sends that actually completed — a send parked on a full channel has not happened
 * yet, and that distinction is the entire subject of §1 and §2.
 *
 * The channel is taken by reference: it is non-movable, and the coordinator below outlives
 * every worker it spawns.
 */
task<void>
counting_producer(channel<int> &ch, int count, std::shared_ptr<int> sent) {
    for (int i = 1; i <= count; ++i) {
        co_await ch.send(i);
        ++(*sent);
    }
}

// ---------------------------------------------------------------------------------------
// 1 + 2. Capacity IS the backpressure policy.
// ---------------------------------------------------------------------------------------
task<void>
demo_capacity(Notes notes) {
    qb::io::cout() << "-- 1. a buffered channel: capacity is how far ahead the producer may run\n";

    {
        channel<int>    ch(3);
        auto            sent = std::make_shared<int>(0);
        coroutine_scope scope;
        scope.spawn(counting_producer(ch, 5, sent));

        // Give the loop a turn with NO consumer running. The producer fills the buffer and
        // then parks inside its fourth `send`.
        co_await sleep(20ms);
        notes->buffered_before_park = *sent;
        qb::io::cout() << "    size()=" << ch.size() << ", capacity()=" << ch.capacity() << ", empty()=" << (ch.empty() ? "true" : "false")
                       << "\n";
        if (*sent == 3)
            qb::io::cout() << "[capacity 3] the producer got 3 sends through and then PARKED inside its fourth — "
                              "the buffer is exactly how far ahead it is allowed to run\n";
        else
            qb::io::cout() << "[capacity 3] UNEXPECTED: " << *sent << " sends completed, not 3\n";

        // Now drain it. Every recv frees a slot and wakes the parked sender.
        std::vector<int> got;
        while (got.size() < 5) {
            auto v = co_await ch.recv();
            if (!v)
                break;
            got.push_back(*v);
        }
        qb::io::cout() << "    draining released the sender: " << got.size() << " values received, " << *sent << " sends completed\n";
        co_await scope.join_all();
    }

    qb::io::cout() << "\n-- 2. a rendezvous channel: capacity 0, which is the default\n";

    {
        channel<int>    ch; // capacity 0
        auto            sent = std::make_shared<int>(0);
        coroutine_scope scope;
        scope.spawn(counting_producer(ch, 3, sent));

        co_await sleep(20ms);
        notes->rendezvous_before_park = *sent;
        if (*sent == 0)
            qb::io::cout() << "[capacity 0] a rendezvous channel let 0 sends through with no receiver — the value is "
                              "handed over, never stored, so a send is a MEETING\n";
        else
            qb::io::cout() << "[capacity 0] UNEXPECTED: " << *sent << " sends completed with no receiver\n";

        // Each recv completes exactly one send. A slow consumer therefore paces the producer,
        // which is what you want when "queue grew to 400 000" is a failure mode.
        for (int i = 0; i < 3; ++i) {
            co_await sleep(15ms);
            auto v = co_await ch.recv();
            qb::io::cout() << "    consumer took " << (v ? *v : -1) << " (" << *sent << " sends now complete)\n";
        }
        co_await scope.join_all();
    }
    qb::io::cout() << "\n";
}

// ---------------------------------------------------------------------------------------
// 3. close() — drain, then nullopt; and a send after close is an error, not a silent drop.
// ---------------------------------------------------------------------------------------
task<void>
demo_close() {
    qb::io::cout() << "-- 3. close(): the shutdown protocol\n";

    channel<std::string> ch(4);
    co_await ch.send("alpha");
    co_await ch.send("beta");
    ch.close();

    qb::io::cout() << "    is_closed()=" << (ch.is_closed() ? "true" : "false") << " but size()=" << ch.size()
                   << ": close DRAINS, it does not discard\n";

    int drained = 0;
    while (auto v = co_await ch.recv()) {
        qb::io::cout() << "    still received \"" << *v << "\" after close\n";
        ++drained;
    }
    qb::io::cout() << "[close] recv() returned nullopt after the buffer drained (" << drained
                   << " values first) — that nullopt is end-of-stream, and it is the ONLY signal a consumer needs\n";

    try {
        co_await ch.send("gamma");
        qb::io::cout() << "[close] UNEXPECTED: the send was accepted\n";
    } catch (const channel_closed &e) {
        qb::io::cout() << "[close] send() on a closed channel threw channel_closed (\"" << e.what()
                       << "\") — loudly, rather than dropping the value where nobody would look for it\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 4 + 5. The non-suspending pair, and the pair with a clock.
// ---------------------------------------------------------------------------------------
task<void>
demo_try_and_timed() {
    qb::io::cout() << "-- 4. try_send / try_recv: the answers you can have without suspending\n";

    channel<int> ch(2);
    qb::io::cout() << "    try_send(1)=" << (ch.try_send(1) ? "true" : "false") << ", try_send(2)=" << (ch.try_send(2) ? "true" : "false")
                   << "\n";
    const bool third = ch.try_send(3);
    if (!third)
        qb::io::cout() << "[try_send] returned false on a full channel — a caller that cannot wait now knows it, "
                          "instead of parking a coroutine it did not mean to park\n";
    else
        qb::io::cout() << "[try_send] UNEXPECTED: the third try_send was accepted by a capacity-2 channel\n";

    auto a = ch.try_recv();
    auto b = ch.try_recv();
    auto c = ch.try_recv();
    qb::io::cout() << "    try_recv() gave " << (a ? std::to_string(*a) : "nullopt") << ", " << (b ? std::to_string(*b) : "nullopt") << ", "
                   << (c ? std::to_string(*c) : "nullopt") << " — the empty one is nullopt, same shape as a closed recv\n\n";

    qb::io::cout() << "-- 5. recv_for / send_for: the same pair, with a clock\n";

    const auto t0    = std::chrono::steady_clock::now();
    auto       timed = co_await ch.recv_for(60ms);
    const auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    qb::io::cout() << "[recv_for] timed out after " << ms << " ms and returned " << (timed ? "a value" : "nullopt")
                   << " — note nullopt means EITHER timeout OR closed, so ask is_closed() if you need to tell them apart\n";

    channel<int> full(1);
    co_await full.send(1);
    const bool accepted = co_await full.send_for(2, 40ms);
    if (!accepted)
        qb::io::cout() << "[send_for] returned false because nobody drained the one slot in 40 ms — the value "
                          "was NOT enqueued, so a caller must decide what to do with it\n\n";
    else
        qb::io::cout() << "[send_for] UNEXPECTED: the send into a full channel was accepted\n\n";
}

// ---------------------------------------------------------------------------------------
// 6. select — the first of N channels to have something to say.
// ---------------------------------------------------------------------------------------
task<void>
feed_after(channel<std::string> &ch, std::string what, qb::duration delay) {
    co_await sleep(delay);
    co_await ch.send(std::move(what));
}

task<void>
close_after(channel<int> &ch, qb::duration delay) {
    co_await sleep(delay);
    ch.close();
}

task<void>
demo_select() {
    qb::io::cout() << "-- 6. select: wait on several channels at once\n";

    channel<int>         numbers;
    channel<std::string> words;

    coroutine_scope scope;
    scope.spawn(feed_after(words, "hello", 30ms));
    scope.spawn(close_after(numbers, 80ms));

    // The channels may hold DIFFERENT types. `select_result` therefore carries the value in a
    // `std::any`, and `index` tells you which `get<T>` is the right one to call. Getting that
    // pairing wrong is a std::bad_any_cast, not a silent misread.
    select_result first = co_await select(numbers, words);
    qb::io::cout() << "[select] two channels, different types; the winner was index " << first.index << " (\""
                   << (first.index == 1 ? first.get<std::string>() : std::string{"?"}) << "\") — index says which get<T> is legal\n";

    // A CLOSE also wakes a select. That is what makes it a usable shutdown signal: one of the
    // channels you are selecting on is the quit channel, and `closed` is how you notice.
    auto second = co_await select(numbers, words);
    if (second.closed && second.index == 0)
        qb::io::cout() << "[select] the second wake reported closed=true on index 0 — a closed channel is an "
                          "EVENT here, which is exactly how you build a quit signal\n\n";
    else
        qb::io::cout() << "[select] UNEXPECTED: index " << second.index << ", closed=" << (second.closed ? "true" : "false") << "\n\n";

    co_await scope.join_all();
}

// ---------------------------------------------------------------------------------------
// 7. Channels composed: filter -> transform -> collect, and the one-line version.
// ---------------------------------------------------------------------------------------
task<void>
emit_then_close(channel<int> &ch, int upto) {
    for (int i = 1; i <= upto; ++i)
        co_await ch.send(i);
    ch.close(); // the head of a pipeline is the only place that decides it is over
}

task<void>
demo_pipeline() {
    qb::io::cout() << "-- 7. composing channels into a pipeline\n";

    channel<int>         raw(4);
    channel<int>         evens(4);
    channel<std::string> text(4);

    coroutine_scope scope;
    scope.spawn(emit_then_close(raw, 8));
    // Each stage closes ITS output when ITS input closes, so one close() at the head
    // propagates the whole way down and nothing has to be shut down twice.
    scope.spawn(filter(raw, evens, [](int v) { return v % 2 == 0; }));
    scope.spawn(transform(evens, text, [](int v) { return "#" + std::to_string(v); }));

    // `collect` is the terminal: it drains until close and hands back a vector.
    auto out = co_await collect(text);
    co_await scope.join_all();

    std::string joined;
    for (const auto &s : out)
        joined += (joined.empty() ? "" : " ") + s;
    qb::io::cout() << "[pipeline] filter -> transform -> collect produced " << out.size() << " values: " << joined << "\n";

    // `make_pipeline` is the same idea packaged: it allocates both channels, spawns the worker,
    // and hands you the two ends. The worker CO-OWNS both channels, so you may drop your own
    // handles whenever you like.
    auto [in_end, out_end] = make_pipeline<int, std::string>([](int v) { return "x" + std::to_string(v); }, 4);
    co_await in_end->send(21);
    auto one = co_await out_end->recv();
    in_end->close();
    co_await sleep(20ms); // let the detached worker see the close and run to its end
    qb::io::cout() << "    make_pipeline packages the same thing: sent 21, received \"" << (one ? *one : std::string{"?"})
                   << "\", and closing the input ended the worker\n";

    // `make_channel<T>(capacity)` is the heap-allocating constructor, for when the channel must
    // outlive the frame that created it.
    auto owned = make_channel<int>(2);
    owned->try_send(99);
    qb::io::cout() << "    make_channel<int>(2) gives a unique_ptr for a channel that must outlive its "
                   << "creating frame; it holds " << owned->size() << " value\n\n";
}

task<void>
run_all(Notes notes) {
    co_await demo_capacity(notes);
    co_await demo_close();
    co_await demo_try_and_timed();
    co_await demo_select();
    co_await demo_pipeline();
}

int
main() {
    qb::io::cout() << "=== channels: a queue between two coroutines, and what its capacity means ===\n\n";

    init();

    auto notes = std::make_shared<Backpressure>();
    run_sync(run_all(notes));

    // Gated on the two backpressure measurements rather than on reaching the last line: a
    // capacity that stopped being enforced would print every other line above unchanged.
    qb::io::cout() << "    measured: " << notes->buffered_before_park << " sends through a capacity-3 channel and "
                   << notes->rendezvous_before_park << " through a capacity-0 one, with no consumer running\n";
    if (notes->buffered_before_park == 3 && notes->rendezvous_before_park == 0)
        qb::io::cout() << "=== channels complete: capacity is the policy, close is the protocol ===\n";
    else
        qb::io::cout() << "=== channels INCONCLUSIVE: the backpressure counts are not 3 and 0 ===\n";
    return 0;
}
