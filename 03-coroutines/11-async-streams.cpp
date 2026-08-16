/**
 * @file examples/03-coroutines/11-async-streams.cpp
 * @tier 03-coroutines
 * @teaches Composing a sequence instead of looping over it: `async_stream<T>` is a chain of
 *          transformations that produces NOTHING until a terminal pulls on it, and the only
 *          sequence library here that treats TIME as a source and as a transform.
 * @demonstrates qb::io::async::async_stream<int>, from_vector, from_channel, single, empty,
 *               map, filter, take, skip, chain, buffer, throttle, debounce, backpressure,
 *               collect, count, first, reduce, any, all, find, for_each, drain_to,
 *               qb::io::async::interval, qb::io::async::timer, qb::io::async::zip,
 *               qb::io::async::merge_streams, qb::io::async::range_stream,
 *               qb::io::async::repeat_value, qb::io::async::channel<int>,
 *               qb::io::async::task<void>, qb::io::async::run_sync
 * @prerequisites 03-coroutines/09-channels, 03-coroutines/10-generators
 * @expect "[lazy] the chain was built and NOTHING ran"
 * @expect "[lazy] collect() pulled the whole source: 8 map calls"
 * @expect "[lazy] the same chain with .take(3) cost 3 map calls"
 * @expect "[terminals] count/first/reduce/any/all/find over one shape"
 * @expect "[channel] from_channel + drain_to bridges a stream and a queue"
 * @expect "[shape] take / skip / chain / buffer restructure the sequence"
 * @expect "[shape] buffer(0) throws std::invalid_argument"
 * @expect "[time] interval(40ms).take(3) took"
 * @expect "[time] throttle(30ms) spread 4 instant values over"
 * @expect "[time] debounce(40ms) turned a burst of 5 into 1 value"
 * @expect "[combine] zip / merge_streams / range_stream / repeat_value"
 * @expect "=== async streams complete: 8 pulls, then 3, from the same source ==="
 *
 * A STREAM IS A FUNCTION, NOT A CONTAINER
 * ---------------------------------------
 * `async_stream<T>` holds exactly one thing: a callable that returns
 * `task<std::optional<T>>` — "give me the next value, or nothing if it is over". Every
 * transform (`map`, `filter`, `take`, …) returns a NEW stream that wraps the previous one's
 * callable. So building a ten-stage pipeline allocates ten small closures and runs zero of
 * them. Work happens only when a TERMINAL (`collect`, `count`, `for_each`, …) starts pulling.
 *
 * That is the first thing this program measures: a counter inside a `map` reads ZERO after the
 * chain is built, then reads exactly as many as were pulled. Adding `.take(3)` changes the
 * number from 8 to 3 without changing anything else, which is laziness being useful rather
 * than laziness being described.
 *
 * WHY IT IS WORTH HAVING ALONGSIDE generator/channel
 * --------------------------------------------------
 *   generator<T>     one producer, synchronous, no composition.
 *   channel<T>       a queue between two coroutines; the consumer writes its own loop.
 *   async_stream<T>  a COMPOSITION. And the only one of the three where time is first class:
 *                    `interval` is a stream of ticks, `throttle` bounds the emission rate, and
 *                    `debounce` collapses a burst into its last value.
 *
 * TERMINALS CONSUME THE STREAM. Every terminal does `std::move(*this)`, so a stream feeds
 * exactly one of them. That is deliberate: the callable's captured cursors are single-use, and
 * a second terminal on the same object would silently see an exhausted source.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-async-streams
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-async-streams
 */

#include <chrono>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// The two measured pull counts: how many times the mapping function actually ran.
struct Pulls {
    int whole_source = -1;
    int with_take    = -1;
};
using Counted = std::shared_ptr<Pulls>;

// ---------------------------------------------------------------------------------------
// 1. Laziness, measured.
// ---------------------------------------------------------------------------------------
task<void>
demo_lazy(Counted counted) {
    qb::io::cout() << "-- 1. a stream is a function: building the chain runs nothing\n";

    std::vector<int> source(8);
    std::iota(source.begin(), source.end(), 1); // 1..8

    auto calls    = std::make_shared<int>(0);
    auto doubling = [calls](int v) {
        ++(*calls);
        return v * 2;
    };

    // Three stages, zero work. `map` copies the previous stage's callable into a new closure;
    // nothing has been asked for a value yet.
    auto chain = async_stream<int>::from_vector(source).map(doubling).filter([](int v) { return v % 4 == 0; });
    if (*calls == 0)
        qb::io::cout() << "[lazy] the chain was built and NOTHING ran — 0 map calls so far\n";
    else
        qb::io::cout() << "[lazy] UNEXPECTED: " << *calls << " map calls before any terminal\n";

    // A terminal pulls. `collect` pulls until end-of-stream, so the whole source is mapped.
    auto kept             = co_await chain.collect();
    counted->whole_source = *calls;
    if (*calls == 8)
        qb::io::cout() << "[lazy] collect() pulled the whole source: 8 map calls, " << kept.size() << " values survived the filter\n";
    else
        qb::io::cout() << "[lazy] UNEXPECTED: collect() cost " << *calls << " map calls, not 8\n";

    // Same source, same map, one extra stage — and the cost changes, because `take` stops
    // pulling. This is the entire practical argument for a lazy pipeline.
    auto calls2    = std::make_shared<int>(0);
    auto doubling2 = [calls2](int v) {
        ++(*calls2);
        return v * 2;
    };
    auto head          = co_await async_stream<int>::from_vector(source).map(doubling2).take(3).collect();
    counted->with_take = *calls2;
    if (*calls2 == 3)
        qb::io::cout() << "[lazy] the same chain with .take(3) cost 3 map calls — the source was never asked "
                          "for its other five values\n\n";
    else
        qb::io::cout() << "[lazy] UNEXPECTED: .take(3) cost " << *calls2 << " map calls, delivering " << head.size() << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. The terminals. Each one consumes the stream it is called on.
// ---------------------------------------------------------------------------------------
task<void>
demo_terminals() {
    qb::io::cout() << "-- 2. terminals: nine ways to end a pipeline\n";

    std::vector<int> nums{3, 1, 4, 1, 5, 9, 2, 6};
    auto             fresh = [&nums] {
        return async_stream<int>::from_vector(nums);
    };

    const auto n       = co_await fresh().count();
    const auto head    = co_await fresh().first();
    const auto total   = co_await fresh().reduce([](int a, int b) { return a + b; }, 0);
    const bool any_big = co_await fresh().any([](int v) { return v > 8; });
    const bool all_pos = co_await fresh().all([](int v) { return v > 0; });
    const auto found   = co_await fresh().find([](int v) { return v % 2 == 0; });

    int visited = 0;
    co_await fresh().for_each([&visited](int) { ++visited; });

    if (n == 8 && head && *head == 3 && total == 31 && any_big && all_pos && found && *found == 4 && visited == 8)
        qb::io::cout() << "[terminals] count/first/reduce/any/all/find over one shape: 8, 3, 31, true, true, 4 — "
                          "and for_each walked all 8 without building anything\n";
    else
        qb::io::cout() << "[terminals] UNEXPECTED: " << n << ", " << (head ? *head : -1) << ", " << total << ", " << any_big << ", " << all_pos
                       << ", " << (found ? *found : -1) << ", " << visited << "\n";

    // `single` and `empty` are the two degenerate sources, and they are worth knowing because
    // they let a function return a stream unconditionally instead of returning an optional one.
    const auto one  = co_await async_stream<std::string>::single("only").count();
    const auto none = co_await async_stream<std::string>::empty().count();
    qb::io::cout() << "    single() has " << one << " element and empty() has " << none
                   << " — a function can always return a stream, never a null one\n\n";
}

// ---------------------------------------------------------------------------------------
// 3. The bridge to channels, in both directions.
// ---------------------------------------------------------------------------------------
task<void>
fill_then_close(channel<int> &ch, int upto) {
    for (int i = 1; i <= upto; ++i)
        co_await ch.send(i);
    ch.close();
}

task<void>
demo_channel_bridge() {
    qb::io::cout() << "-- 3. streams and channels are two views of the same thing\n";

    channel<int> in(8);
    channel<int> out(8);

    coroutine_scope scope;
    scope.spawn(fill_then_close(in, 5));

    // `from_channel` borrows the channel BY REFERENCE — it must outlive the stream and every
    // coroutine consuming it. `from_channel_shared` is the version without that obligation, and
    // it is what you want the moment the stream is stored anywhere.
    co_await async_stream<int>::from_channel(in).map([](int v) { return v * 100; }).drain_to(out);
    out.close();

    auto received = co_await collect(out);
    co_await scope.join_all();

    std::string text;
    for (auto v : received)
        text += std::to_string(v) + " ";
    if (received.size() == 5)
        qb::io::cout() << "[channel] from_channel + drain_to bridges a stream and a queue: " << text << "\n";
    else
        qb::io::cout() << "[channel] UNEXPECTED: " << received.size() << " values came out\n";

    // `backpressure(n)` puts a bounded channel INSIDE the pipeline and a filler coroutine in
    // front of it, so a fast source cannot run more than n values ahead of a slow terminal.
    auto paced = co_await async_stream<int>::from_vector(std::vector<int>{1, 2, 3, 4, 5, 6}).backpressure(2).collect();
    qb::io::cout() << "    backpressure(2) put a 2-slot channel inside the pipeline; " << paced.size()
                   << " values still came out, just never more than two ahead\n\n";
}

// ---------------------------------------------------------------------------------------
// 4. Reshaping: take / skip / chain / buffer.
// ---------------------------------------------------------------------------------------
task<void>
demo_shape() {
    qb::io::cout() << "-- 4. reshaping a sequence without materialising it\n";

    std::vector<int> v(10);
    std::iota(v.begin(), v.end(), 0); // 0..9

    auto tail    = co_await async_stream<int>::from_vector(v).skip(7).collect();
    auto joined  = co_await async_stream<int>::from_vector(v).take(2).chain(async_stream<int>::single(99)).collect();
    auto batched = co_await async_stream<int>::from_vector(v).buffer(4).collect();

    if (tail.size() == 3 && joined.size() == 3 && joined.back() == 99 && batched.size() == 3 && batched.back().size() == 2)
        qb::io::cout() << "[shape] take / skip / chain / buffer restructure the sequence: skip(7) left 3, "
                          "chain appended 99, buffer(4) made 3 batches of 4/4/2\n";
    else
        qb::io::cout() << "[shape] UNEXPECTED: " << tail.size() << ", " << joined.size() << ", " << batched.size() << "\n";

    // `buffer(0)` would build a stream that pulls nothing and yields nothing — total, silent
    // data loss on an argument that looks legal. It is rejected instead.
    try {
        auto bad = async_stream<int>::from_vector(v).buffer(0);
        qb::io::cout() << "[shape] UNEXPECTED: buffer(0) was accepted\n\n";
    } catch (const std::invalid_argument &e) {
        qb::io::cout() << "[shape] buffer(0) throws std::invalid_argument (\"" << e.what()
                       << "\") rather than yielding an empty stream nobody could diagnose\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 5. TIME. This is what async_stream has that generator and channel do not.
// ---------------------------------------------------------------------------------------
task<void>
demo_time() {
    qb::io::cout() << "-- 5. time as a source and as a transform\n";

    // `interval(d)` is an INFINITE stream of tick numbers, one every d. It is only usable
    // because `take` can bound it — which is the lazy-pipeline argument again, now load-bearing.
    const auto t0      = std::chrono::steady_clock::now();
    auto       ticks   = co_await interval(40ms).take(3).collect();
    const auto tick_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    qb::io::cout() << "[time] interval(40ms).take(3) took " << tick_ms << " ms and emitted ticks " << ticks.front() << ".." << ticks.back()
                   << " — the tick number is the value\n";

    // `timer(v, d)` is the one-shot: a single value, after a delay, then end-of-stream.
    const auto t1       = std::chrono::steady_clock::now();
    auto       late     = co_await timer(std::string{"fired"}, 50ms).collect();
    const auto timer_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
    qb::io::cout() << "    timer(\"fired\", 50ms) produced " << late.size() << " value after " << timer_ms << " ms\n";

    // `throttle(d)` bounds the RATE. The source below is instantaneous; throttling spreads it.
    const auto t2        = std::chrono::steady_clock::now();
    auto       spread    = co_await range_stream(0, 4).throttle(30ms).collect();
    const auto spread_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t2).count();
    if (spread.size() == 4 && spread_ms >= 80)
        qb::io::cout() << "[time] throttle(30ms) spread 4 instant values over " << spread_ms
                       << " ms — one emission per interval, and the source is made to wait\n";
    else
        qb::io::cout() << "[time] UNEXPECTED: throttle produced " << spread.size() << " values in " << spread_ms << " ms\n";

    // `debounce(d)` is the opposite question: not "how often may I emit" but "has it gone
    // quiet yet". A burst of five arriving at once collapses to the LAST one.
    auto burst = co_await async_stream<int>::from_vector(std::vector<int>{1, 2, 3, 4, 5}).debounce(40ms).collect();
    if (burst.size() == 1 && burst.front() == 5)
        qb::io::cout() << "[time] debounce(40ms) turned a burst of 5 into 1 value, and it is the LAST one — "
                          "which is what a search box wants and what a rate limiter does not\n\n";
    else
        qb::io::cout() << "[time] UNEXPECTED: debounce produced " << burst.size() << " values\n\n";
}

// ---------------------------------------------------------------------------------------
// 6. Combining streams.
// ---------------------------------------------------------------------------------------
task<void>
demo_combine() {
    qb::io::cout() << "-- 6. combining several streams into one\n";

    // `zip` walks two streams in lockstep and STOPS at the shorter one, evaluating left first
    // so a slow right-hand stream is never pulled after the left has ended.
    auto pairs = co_await zip(range_stream(1, 5), async_stream<std::string>::from_vector(std::vector<std::string>{"a", "b"})).collect();

    // `merge_streams` interleaves round-robin and ends when every input has ended.
    std::vector<async_stream<int>> parts;
    parts.push_back(range_stream(10, 13));
    parts.push_back(range_stream(20, 22));
    auto merged = co_await merge_streams(std::move(parts)).collect();

    // `repeat_value` is infinite, which is only ever useful next to a `take`.
    auto echoes = co_await repeat_value(std::string{"ping"}).take(3).collect();

    std::string merged_text;
    for (auto v : merged)
        merged_text += std::to_string(v) + " ";
    if (pairs.size() == 2 && merged.size() == 5 && echoes.size() == 3)
        qb::io::cout() << "[combine] zip / merge_streams / range_stream / repeat_value: zip stopped at the "
                          "shorter side with 2 pairs, merge interleaved 5 values, repeat_value gave 3 pings\n";
    else
        qb::io::cout() << "[combine] UNEXPECTED: " << pairs.size() << ", " << merged.size() << ", " << echoes.size() << "\n";
    qb::io::cout() << "    merged round-robin order was " << merged_text << "\n\n";
}

task<void>
run_all(Counted counted) {
    co_await demo_lazy(counted);
    co_await demo_terminals();
    co_await demo_channel_bridge();
    co_await demo_shape();
    co_await demo_time();
    co_await demo_combine();
}

int
main() {
    qb::io::cout() << "=== async streams: a pipeline that produces nothing until you pull ===\n\n";

    init();

    auto counted = std::make_shared<Pulls>();
    run_sync(run_all(counted));

    // Gated on the two measured pull counts. They are the claim: the same source, the same
    // map, and a different amount of work because of one extra stage.
    qb::io::cout() << "    measured: " << counted->whole_source << " map calls for collect(), " << counted->with_take
                   << " for the same chain with .take(3)\n";
    if (counted->whole_source == 8 && counted->with_take == 3)
        qb::io::cout() << "=== async streams complete: 8 pulls, then 3, from the same source ===\n";
    else
        qb::io::cout() << "=== async streams INCONCLUSIVE: the pull counts are not 8 and 3 ===\n";
    return 0;
}
