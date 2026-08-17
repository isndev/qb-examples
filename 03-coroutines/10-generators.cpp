/**
 * @file examples/03-coroutines/10-generators.cpp
 * @tier 03-coroutines
 * @teaches Producing a sequence one value at a time instead of returning a container:
 *          `generator<T>` when the production is synchronous, `async_generator<T>` when it has
 *          to await. Laziness is the point, so this program counts what the source actually
 *          produced rather than trusting that it stopped when the consumer did.
 * @demonstrates qb::io::async::generator<int>, qb::io::async::async_generator<int>,
 *               qb::io::async::range, qb::io::async::iota, qb::io::async::take,
 *               qb::io::async::skip, qb::io::async::concat, qb::io::async::repeat_n,
 *               qb::io::async::from_range, qb::io::async::collect_to_vector, has_next, next,
 *               qb::io::async::ag_collect, qb::io::async::ag_map, qb::io::async::ag_filter,
 *               qb::io::async::ag_reduce, qb::io::async::ag_take, qb::io::async::ag_for_each,
 *               qb::io::async::task<void>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/01-first-coroutine, 03-coroutines/09-channels
 * @expect "[generator] range(1, 6) yielded 5 values"
 * @expect "[generator] the source produced exactly 3 values to satisfy take(gen, 3)"
 * @expect "[generator] has_next()/next() is the same walk, spelled by hand"
 * @expect "[generator] concat / skip / repeat_n / from_range compose"
 * @expect "[async_generator] a co_await BETWEEN yields is the whole difference"
 * @expect "[async_generator] ag_take(gen, 3) pulled exactly 3"
 * @expect "[async_generator] ag_map / ag_filter / ag_reduce over one source"
 * @expect "=== generators complete: 3 pulled for a sync take of 3, 3 for an async one ==="
 *
 * WHAT A GENERATOR IS FOR
 * -----------------------
 * A function that returns `std::vector<Row>` has already done all the work and paid for all
 * the memory by the time you look at the first row. A function that returns `generator<Row>`
 * has done none of it: the body runs up to its first `co_yield`, hands you that value, and
 * FREEZES there until you ask for the next one. Ten million rows cost one row of memory, and
 * a consumer that stops after three has caused three rows of work.
 *
 * THE TWO KINDS, AND WHY BOTH EXIST
 * ---------------------------------
 *   generator<T>        synchronous. `co_yield` only — `co_await` inside one is a COMPILE
 *                       ERROR, because its promise declares `await_transform() = delete`. That
 *                       is deliberate: consuming it is an ordinary `for` loop with no
 *                       suspension, so it can be used from code that is not a coroutine at all.
 *   async_generator<T>  the same shape, but the body may `co_await` between yields — read a
 *                       socket, wait on a timer, query a database. The price is that consuming
 *                       it is itself an await: `while (auto v = co_await gen.next())`.
 *
 * Reach for `generator` for a pure sequence (a parser's tokens, a range, a decoded frame list)
 * and for `async_generator` the moment producing the NEXT value requires waiting.
 *
 * ONE MEASUREMENT WORTH KEEPING
 * ------------------------------
 * `take(gen, n)` pulls exactly `n` values from its source — never `n + 1` — and so does the
 * async `ag_take`. That agreement is not free, and this program measures it rather than
 * asserting it: an earlier `take()` was a range-for that pulled a value and *then* decided it
 * was past the limit, so a `take(gen, 3)` fetched a fourth value and discarded it. Over
 * `iota` that costs nothing, which is exactly why it survived; over a generator whose body
 * consumes a row, a token or a byte from a socket, that pull is a side effect nobody asked
 * for. Writing this program is what surfaced it. Both counts are measured below and the
 * summary is gated on them, so the two halves of the pair can never drift apart again
 * unnoticed.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-generators
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-generators
 */

#include <chrono>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// THE INSTRUMENT. Both counting sources below increment this once per value they PRODUCE, so
// the gap between "produced" and "consumed" is the measurement. A shared_ptr because a
// generator body outlives the statement that created it.
using Tally = std::shared_ptr<int>;

// A synchronous source that is infinite AND observable. `co_await` inside this body would not
// compile: `generator<T>::promise_type` declares `void await_transform() = delete;`.
generator<int>
counting_source(Tally produced) {
    int n = 0;
    while (true) {
        ++(*produced);
        co_yield ++n;
    }
}

// The async twin: it WAITS between values, which is the one thing the synchronous generator
// cannot do. The `sleep` here stands in for the socket read or the query that makes a source
// asynchronous in the first place.
async_generator<int>
async_counting_source(Tally produced) {
    int n = 0;
    while (true) {
        co_await sleep(5ms);
        ++(*produced);
        co_yield ++n;
    }
}

// ---------------------------------------------------------------------------------------
// 1. generator<T> — the synchronous, pull-driven sequence.
// ---------------------------------------------------------------------------------------
void
demo_generator(int &sync_pulled) {
    qb::io::cout() << "-- 1. generator<T>: values appear when you ask for them\n";

    // `range(start, end)` is the finite one. It is an ordinary range-for: no engine, no
    // scheduler, no coroutine on the consuming side. That is the whole point of the
    // synchronous flavour.
    std::vector<int> seen;
    for (auto v : range(1, 6))
        seen.push_back(v);
    qb::io::cout() << "[generator] range(1, 6) yielded 5 values: " << seen.size() << " of them, first " << seen.front() << ", last "
                   << seen.back() << " — and no event loop was involved\n";

    // Now the measurement. `iota(1)` and `counting_source` are both INFINITE; taking three
    // from them terminates because the consumer stops asking, which is laziness working.
    // The whole pipeline is one expression: every combinator in this family (take, skip,
    // concat, from_range) takes its source BY VALUE, and `collect_to_vector` accepts a
    // temporary too, so a chain needs no named intermediate.
    auto produced = std::make_shared<int>(0);
    auto first3   = collect_to_vector(take(counting_source(produced), 3));
    sync_pulled   = *produced;

    qb::io::cout() << "    take(gen, 3) handed back " << first3.size() << " values and its source produced " << *produced << "\n";
    if (*produced == 3 && first3.size() == 3)
        qb::io::cout() << "[generator] the source produced exactly 3 values to satisfy take(gen, 3) — take() tests "
                          "the limit BEFORE it resumes the source, so there is no discarded fourth pull\n";
    else
        qb::io::cout() << "[generator] UNEXPECTED: " << *produced << " produced for a take of 3\n";
    qb::io::cout() << "    an over-pull would be free for iota(); it would not be free for a source whose body "
                      "consumes a row, a token or a byte\n";

    // `has_next()` / `next()` is the same walk written by hand, for when a range-for does not
    // fit — a state machine, an interleave, a caller that wants to stop on a value's content.
    auto        letters = from_range(std::vector<std::string>{"a", "b", "c"});
    std::string joined;
    while (letters.has_next()) {
        auto v = letters.next();
        if (!v)
            break; // has_next() is a hint; next() returning nullopt is the authority
        joined += *v;
    }
    qb::io::cout() << "[generator] has_next()/next() is the same walk, spelled by hand: \"" << joined << "\"\n";

    // The combinators are themselves generators, so composing them costs one frame each and
    // still produces nothing until something pulls.
    auto        chain     = concat(repeat_n(0, 2), skip(range(10, 20), 7));
    auto        composed  = collect_to_vector(chain);
    auto        head      = take(iota(100), 3);
    auto        from_iota = collect_to_vector(head);
    std::string composed_text;
    for (auto v : composed)
        composed_text += std::to_string(v) + " ";
    qb::io::cout() << "[generator] concat / skip / repeat_n / from_range compose into one lazy chain: " << composed_text
                   << "and take(iota(100), 3) gave " << from_iota.front() << ".." << from_iota.back() << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. async_generator<T> — the same shape, with a suspension point inside the producer.
// ---------------------------------------------------------------------------------------
task<void>
demo_async_generator(std::shared_ptr<int> async_pulled) {
    qb::io::cout() << "-- 2. async_generator<T>: a producer that is allowed to wait\n";

    // Consuming it is an await, because producing the next value is. `next()` hands back
    // `std::optional<T>` and an empty one is end-of-stream — the same shape a channel's
    // `recv()` uses, deliberately.
    auto        produced = std::make_shared<int>(0);
    auto        gen      = async_counting_source(produced);
    std::string got;
    for (int i = 0; i < 3; ++i) {
        auto v = co_await gen.next();
        if (!v)
            break;
        got += std::to_string(*v) + " ";
    }
    qb::io::cout() << "[async_generator] a co_await BETWEEN yields is the whole difference: got " << got << "after three pulls, each "
                   << "of which cost a 5 ms wait that a synchronous generator could not have taken\n";

    // ag_take is the async twin of take(), and it compares the count BEFORE awaiting the next
    // value, so a source with side effects sees exactly n. The synchronous take() agrees.
    auto strict   = std::make_shared<int>(0);
    auto taken    = co_await ag_collect(ag_take(async_counting_source(strict), 3));
    *async_pulled = *strict;
    if (*strict == 3 && taken.size() == 3)
        qb::io::cout() << "[async_generator] ag_take(gen, 3) pulled exactly 3 — the async twin compares the count "
                          "BEFORE it pulls, and the synchronous take() now does the same\n";
    else
        qb::io::cout() << "[async_generator] UNEXPECTED: ag_take pulled " << *strict << " for 3 values\n";

    // The ag_* family are terminals: each drains a generator and hands back a value. They take
    // the generator BY VALUE, so a generator can feed exactly one of them.
    auto bounded = [](std::shared_ptr<int> t) {
        return ag_take(async_counting_source(std::move(t)), 4);
    };
    auto sink = std::make_shared<int>(0);

    auto doubled = co_await ag_map(bounded(sink), [](int v) { return v * 2; });
    auto evens   = co_await ag_filter(bounded(sink), [](int v) { return v % 2 == 0; });
    auto total   = co_await ag_reduce(bounded(sink), 0, std::plus<int>{});

    qb::io::cout() << "[async_generator] ag_map / ag_filter / ag_reduce over one source shape: " << doubled.size() << " doubled ("
                   << doubled.front() << ".." << doubled.back() << "), " << evens.size() << " even, sum " << total << "\n";

    int visited = 0;
    co_await ag_for_each(bounded(sink), [&visited](int) { ++visited; });
    qb::io::cout() << "    ag_for_each walked " << visited << " values without building a container at all\n\n";
}

int
main() {
    qb::io::cout() << "=== generators: a sequence you do not materialise ===\n\n";

    init();

    // The synchronous half needs no loop at all — that is the claim, so it is run BEFORE
    // anything is driven, from plain main().
    int sync_pulled = -1;
    demo_generator(sync_pulled);

    auto async_pulled = std::make_shared<int>(-1);
    run_sync(demo_async_generator(async_pulled));

    // Gated on the two measured pull counts rather than on reaching the last line. They must
    // AGREE: two spellings of "take n" that consume different amounts of their source is the
    // defect this program was written to measure, and the gate is what keeps them together.
    qb::io::cout() << "    measured: a synchronous take(gen, 3) pulled " << sync_pulled << ", an ag_take(gen, 3) pulled " << *async_pulled
                   << "\n";
    if (sync_pulled == 3 && *async_pulled == 3)
        qb::io::cout() << "=== generators complete: 3 pulled for a sync take of 3, 3 for an async one ===\n";
    else
        qb::io::cout() << "=== generators INCONCLUSIVE: the pull counts are not 3 and 3 ===\n";
    return 0;
}
