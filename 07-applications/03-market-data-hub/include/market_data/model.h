/*
 * market-data-hub — the domain, and every event that crosses a core.
 *
 * THE ONE RULE THIS FILE EXISTS TO OBEY. A cross-core event is memcpy-RELOCATED (twice) and its
 * source destructor never runs, so no payload here may hold a pointer INTO ITSELF. That rules out
 * a short `std::string` on libstdc++ — where the characters live inside the object — and it is
 * invisible on macOS/libc++, which is why the rule is written down rather than discovered.
 * `qb::string<N>` is an array plus a length: trivially copyable, and the right type for a symbol.
 *
 * See `01-actors/03-event-payloads` for the measurement this rule comes from.
 */
#pragma once

#include <cstdint>
#include <vector>
#include <qb/event.h>
#include <qb/string.h>

namespace market_data {

/// How many symbols the feed produces, and how many ticks per symbol.
inline constexpr int kSymbols        = 8;
inline constexpr int kTicksPerSymbol = 2500;
inline constexpr int kTotalTicks     = kSymbols * kTicksPerSymbol;
/// One aggregator per shard; each symbol is stuck to exactly one of them.
inline constexpr int kAggregators = 3;
/// Coalescing: publish at 64 quotes or 4 ms, whichever comes first.
inline constexpr std::size_t kBatchMax = 64;

using Symbol = qb::string<8>;

/// What the foreign feed thread writes into the spsc ring. A POD, by static_assert below: the
/// ring's bulk paths are `memcpy` and it refuses anything else.
struct Tick {
    std::uint64_t at_ns;  ///< when the feed produced it — the start of the latency measurement
    std::uint32_t seq;    ///< per-symbol sequence, so a gap is visible
    std::uint32_t sym_ix; ///< index into the symbol table
    double        price;
    std::uint32_t volume;
};

static_assert(std::is_trivially_copyable_v<Tick>, "spsc::ringbuffer<T, N> memcpys its bulk paths and refuses any other T");

/// The hot-path event: ingest -> aggregator. Trivially destructible, which is what makes `send<>`
/// (unordered, no destructor bookkeeping) legal for it.
struct TickEvent : qb::Event {
    Symbol        symbol;
    std::uint64_t at_ns{};
    std::uint32_t seq{};
    double        price{};
    std::uint32_t volume{};

    // A constructor rather than aggregate initialisation, because `send<E>(dest, args...)`
    // CONSTRUCTS the event in place from `args` — it does not hand you a reference to fill in the
    // way `push<E>` does. It returns void.
    TickEvent() = default;
    TickEvent(Symbol s, std::uint64_t at, std::uint32_t sq, double p, std::uint32_t v)
        : symbol(s)
        , at_ns(at)
        , seq(sq)
        , price(p)
        , volume(v) {}
};

static_assert(std::is_trivially_destructible_v<TickEvent>, "send<E>() is for trivially-destructible events only; use push<E>() otherwise");

/// One published quote: the aggregator's view of a symbol after a tick.
struct Quote {
    Symbol        symbol;
    std::uint64_t at_ns{}; ///< carried from the tick, so the publisher can measure end to end
    double        last{};
    double        vwap{};
    std::uint32_t volume{};
    std::uint32_t updates{};
};

/// aggregator -> publisher, one batch at a time. Not a hot-path event: it carries a vector, so it
/// owns heap and must travel by `push` (ordered), never `send`.
struct QuoteBatch : qb::Event {
    std::vector<Quote> quotes;
};

/// ingest -> aggregators, once, when the feed is exhausted.
struct EndOfFeed : qb::Event {};
/// aggregator -> publisher, once each, after its final flush.
struct AggregatorDone : qb::Event {
    std::uint32_t emitted{};
};
/// subscriber -> publisher: the wire side has seen the end-of-stream sentinel.
struct SubscriberDone : qb::Event {
    std::uint32_t records{};
};

/// What the engine did, filled in by the actors and PRINTED BY main(). Keeping the report in one
/// place — and out of the actors — is why `main.cpp` can promise what it prints.
struct Report {
    std::vector<std::uint64_t> latencies;      ///< one sample per published quote, nanoseconds
    std::uint64_t              routed{};       ///< ticks the ingest actor took off the ring
    std::uint32_t              shards{};       ///< how many aggregators they were spread across
    std::uint32_t              quotes{};       ///< quotes published
    std::uint32_t              emitted{};      ///< quotes the aggregators say they emitted
    std::uint32_t              wire_records{}; ///< records the subscriber decoded
    Symbol                     last_symbol;
    double                     last_price{};
    double                     last_vwap{};
    std::uint32_t              last_updates{};
};

/// The symbol table. Fixed, so `sym_ix` is all the feed thread has to carry.
inline const char *const kSymbolNames[kSymbols] = {"AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META", "TSLA", "AMD"};

} // namespace market_data
