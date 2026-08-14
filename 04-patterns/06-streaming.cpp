/**
 * @file examples/04-patterns/06-streaming.cpp
 * @tier 04-patterns
 * @teaches One request, MANY replies. `qb::ask_stream` gives the asker a `qb::stream<E>` it
 *          drains with `while (auto chunk = co_await s.next())`, while the responder pushes
 *          with `yield_answer` and finishes with `end_stream` — plus the two ways a stream ends
 *          badly, and why a bounded buffer that throws beats an unbounded one that grows.
 * @demonstrates qb::StreamRequest, qb::ask_stream, qb::stream<Tail>,
 *               yield_answer, qb::end_stream, qb::stream_overflow_error,
 *               qb::io::async::timeout_error, resolve_ask, spawn, registerEvent<E>, push<E>,
 *               qb::Main, ctx.time
 * @prerequisites 04-patterns/04-scatter-gather
 * @expect "[stream] scenario 1: drained "
 * @expect "[stream] scenario 2: one chunk, then timeout_error after"
 * @expect "[stream] scenario 3: buffer 4, producer sent 50, consumer got"
 * @expect "=== streaming complete: many replies per request, back-pressure included ==="
 *
 * WHERE THIS FITS
 * ---------------
 * `qb::ask` is single-reply: one request, one response, done. A cursor, a tail -f, a paginated
 * query, a progress feed and a subscription are all the same shape and none of them fit that —
 * they are one request and N replies, ended by the responder. That is `ask_stream`, and it
 * rides the same per-core continuation registry as `ask`, so it works from inside `onInit()`
 * too (chunks reach an actor that is still Activating).
 *
 * THE FOUR THINGS TO KNOW BEFORE USING IT
 * ---------------------------------------
 * 1. `ask_stream` IS NOT AWAITED. It returns a `qb::stream<E>` synchronously — the awaiting
 *    happens on `s.next()`. `co_await qb::ask_stream(...)` does not compile, and that surprises
 *    everybody once.
 * 2. THE ASKER STILL NEEDS `resolve_ask`. Chunks arrive as ordinary events carrying the stream
 *    id; `void on(Tail &e) { resolve_ask(e); }` is what routes them into the parked `next()`.
 * 3. `next()` YIELDS `std::nullopt` AT END OF STREAM and THROWS on everything else: a per-chunk
 *    `timeout_error` when the responder goes quiet, `cancelled_error` if the asker is killed
 *    while parked, and `stream_overflow_error` if the responder outran the buffer. The `while
 *    (auto chunk = co_await s.next())` loop handles the normal end; a `try` around it handles
 *    rest. A stream with no `end_stream` does not end — it times out, which is scenario 2.
 * 4. THE BUFFER IS BOUNDED AND OVERFLOW IS LOUD. `capacity` (default 256) chunks may sit ahead
 *    of the consumer; past that the stream fails with `stream_overflow_error` rather than
 *    dropping chunks quietly or growing without limit. Scenario 3 provokes it deliberately:
 *    a responder that can produce faster than its consumer can consume is the normal case, and
 *    finding out is worth more than a silent gap in the data.
 *
 * A note on the producer below: it paces itself by pushing an event to ITSELF rather than by
 * spawning a coroutine that captures `this`. Both work, but a self-push needs no lifetime
 * reasoning at all — there is no frame to be resumed after the actor is gone.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-streaming
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-streaming
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// The exchange. `StreamRequest<Chunk>` supplies the `chunk` slot, the `stream_done` end marker
// and the stream id; the request fields are yours. Both string types are bounded because an
// event is memcpy-relocated.
struct Tail : qb::StreamRequest<qb::string<48>> {
    qb::string<16> file;
    int            lines{0};
    int            mode{0}; ///< 0 = normal, 1 = go quiet after one chunk, 2 = flood
};

// The producer's self-tick: "emit the next chunk".
struct Pump : public qb::Event {};

// ---------------------------------------------------------------------------
// The responder. It keeps a copy of the request it is serving, because `yield_answer` needs it
// (that copy carries the requester's id and the stream id).
// ---------------------------------------------------------------------------
class LogTailer : public qb::Actor {
    Tail _serving;
    int  _left = 0;
    bool _busy = false;

    static qb::string<48>
    line_for(qb::string<16> const &file, int n) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s:%d ready", file.c_str(), n);
        return qb::string<48>{buf};
    }

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Tail>(*this);
        registerEvent<Pump>(*this);
        co_return true;
    }

    void
    on(Tail &e) {
        if (e.mode == 2) {
            // Flood: 50 chunks inside ONE handler, so the consumer cannot possibly keep up.
            // This is what a bounded buffer is for.
            for (int i = 0; i < 50; ++i)
                qb::yield_answer(*this, e, line_for(e.file, i));
            qb::end_stream(*this, e);
            return;
        }
        _serving = e; // the copy carries `correlation_id` and the source: that IS the stream
        _left    = e.lines;
        _busy    = true;
        push<Pump>(id());
    }

    void
    on(Pump const &) {
        if (!_busy)
            return;
        if (_left > 0) {
            qb::yield_answer(*this, _serving, line_for(_serving.file, _serving.lines - _left));
            --_left;
            push<Pump>(id()); // one chunk per mailbox turn — incremental, and no timer needed
            return;
        }
        _busy = false;
        if (_serving.mode == 1)
            return; // deliberately never end the stream: the consumer must time out
        qb::end_stream(*this, _serving);
    }
};

// ---------------------------------------------------------------------------
// The consumer. Three scenarios, one coroutine, straight down the page.
// ---------------------------------------------------------------------------
class Reader : public qb::Actor {
    qb::ActorId _tailer;

public:
    explicit Reader(qb::ActorId tailer)
        : _tailer(tailer) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Tail>(*this); // chunks arrive here before they reach the parked next()
        auto tailer = _tailer;
        spawn([tailer](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- 1. the normal shape ------------------------------------------------------
            Tail req;
            req.file  = "app.log";
            req.lines = 5;
            // The type is spelled out once, here, because `ask_stream` is NOT awaited: it hands
            // back a `qb::stream<E>` synchronously and the awaiting happens on `next()`.
            qb::stream<Tail> s = qb::ask_stream(ctx, tailer, req, qb::duration{1s});
            int              n = 0;
            qb::string<48>   last;
            while (auto chunk = co_await s.next()) { // nullopt == the responder said end_stream
                ++n;
                last = chunk->chunk;
            }
            qb::io::cout() << "[stream] scenario 1: drained " << n << " chunks in order, last was " << last.c_str() << ", then end-of-stream\n";

            // ---- 2. a responder that goes quiet -------------------------------------------
            Tail stall;
            stall.file  = "stuck.log";
            stall.lines = 1;
            stall.mode  = 1;
            auto t0     = ctx.time();
            auto s2     = qb::ask_stream(ctx, tailer, stall, qb::duration{150ms});
            int  n2     = 0;
            try {
                while (auto chunk = co_await s2.next())
                    ++n2;
                qb::io::cout() << "[stream] UNREACHABLE: a stream with no end marker ended\n";
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[stream] scenario 2: one chunk, then timeout_error after " << ms_since(ctx, t0)
                               << " ms — the per-chunk timeout is what bounds "
                                  "a stream, not the total\n";
                (void) n2;
            }

            // ---- 3. a responder that outruns its consumer ---------------------------------
            Tail flood;
            flood.file = "firehose.log";
            flood.mode = 2;
            auto s3    = qb::ask_stream(ctx, tailer, flood, qb::duration{1s}, /*capacity=*/4);
            int  n3    = 0;
            try {
                while (auto chunk = co_await s3.next())
                    ++n3;
                qb::io::cout() << "[stream] UNREACHABLE: 50 chunks fitted in a buffer of 4\n";
            } catch (qb::stream_overflow_error const &) {
                qb::io::cout() << "[stream] scenario 3: buffer 4, producer sent 50, consumer got " << n3
                               << " then stream_overflow_error — LOUD, not a silent gap\n";
            }

            qb::io::cout() << "=== streaming complete: many replies per request, back-pressure included ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    // Chunks are AskEvents: this is what hands each one to the parked `next()`.
    void
    on(Tail &e) {
        (void) resolve_ask(e);
    }

private:
    static std::uint64_t
    ms_since(qb::ScopedCoroContext const &ctx, std::uint64_t t0) {
        return (ctx.time() - t0) / 1'000'000ull;
    }
};

int
main() {
    qb::Main engine;

    auto tailer = engine.addActor<LogTailer>(0);
    engine.addActor<Reader>(0, tailer);

    qb::io::cout() << "[main] one tailer, one reader, three ways a stream can end\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
