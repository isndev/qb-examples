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

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>
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
    std::uint64_t at_ns;  ///< feed timestamp; Publisher handler entry closes this measurement
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

/// Every tick contributes to aggregates, even when unordered delivery makes an older tick
/// arrive last. Only the greatest per-symbol sequence may replace the latest price.
struct SymbolBook {
    double        vwap_num = 0;
    std::uint64_t vwap_den = 0;
    double        last     = 0;
    std::uint32_t last_seq = 0;
    std::uint32_t volume   = 0;
    std::uint32_t updates  = 0;

    void
    accept(std::uint32_t seq, double price, std::uint32_t tick_volume) noexcept {
        vwap_num += price * tick_volume;
        vwap_den += tick_volume;
        if (updates == 0 || seq > last_seq) {
            last     = price;
            last_seq = seq;
        }
        volume += tick_volume;
        ++updates;
    }
};

/// The terminal event may overtake unordered ticks. A shard finishes only after it has seen
/// the exact number of ticks the ingest actor sent to it.
struct ShardDrain {
    std::uint32_t                         received{};
    std::uint32_t                         expected{};
    bool                                  ended{};
    std::chrono::steady_clock::time_point deadline{};

    void
    tick() noexcept {
        ++received;
    }
    void
    end(std::uint32_t count, std::chrono::steady_clock::time_point now,
        std::chrono::steady_clock::duration timeout = std::chrono::seconds(20)) noexcept {
        expected = count;
        ended    = true;
        deadline = now + timeout;
    }
    [[nodiscard]] bool
    complete() const noexcept {
        return ended && received == expected;
    }
    [[nodiscard]] bool
    overrun() const noexcept {
        return ended && received > expected;
    }
    [[nodiscard]] bool
    expired(std::chrono::steady_clock::time_point now) const noexcept {
        return ended && !complete() && now >= deadline;
    }
};

/// One published quote: the aggregator's view of a symbol after a tick.
struct Quote {
    Symbol        symbol;
    std::uint64_t at_ns{}; ///< carried from the tick to the publisher handler
    double        last{};
    double        vwap{};
    std::uint32_t volume{};
    std::uint32_t updates{};
    std::uint32_t last_seq{};
};

/// aggregator -> publisher, one batch at a time. Not a hot-path event: it carries a vector, so it
/// owns heap and must travel by `push` (ordered), never `send`.
struct QuoteBatch : qb::Event {
    std::vector<Quote> quotes;
};

/// ingest -> aggregators, once, carrying the exact count for this shard.
struct EndOfFeed : qb::Event {
    std::uint32_t expected{};
};
/// aggregator -> publisher, once each, after its final flush.
struct AggregatorDone : qb::Event {
    std::uint32_t emitted{};
};
/// subscriber -> publisher: the wire side has seen the end-of-stream sentinel.
struct SubscriberDone : qb::Event {
    std::uint32_t records{};
};

/// Reference built by the foreign producer, independent of actor delivery order.
struct FeedSymbolReference {
    long double   weighted_sum{};
    std::uint64_t volume{};
    double        last_price{};
    std::uint32_t last_seq{};
    std::uint32_t count{};
};

struct FeedReference {
    std::array<FeedSymbolReference, kSymbols> symbols{};
    std::uint64_t                             produced{};

    void
    record(Tick const &tick) noexcept {
        auto &symbol = symbols[tick.sym_ix];
        symbol.weighted_sum += static_cast<long double>(tick.price) * tick.volume;
        symbol.volume += tick.volume;
        if (symbol.count == 0 || tick.seq > symbol.last_seq) {
            symbol.last_price = tick.price;
            symbol.last_seq   = tick.seq;
        }
        ++symbol.count;
        ++produced;
    }
};

struct WireSymbolObservation {
    std::uint64_t last_micros{};
    std::uint64_t vwap_micros{};
    std::uint32_t volume{};
    std::uint32_t updates{};
    std::uint32_t last_seq{};
    std::uint32_t records{};
    bool          seen{};
};

/// What the engine did, filled in by the actors and PRINTED BY main(). Keeping the report in one
/// place — and out of the actors — is why `main.cpp` can promise what it prints.
struct Report {
    std::vector<std::uint64_t>                  latencies;                ///< one sample per published quote, nanoseconds
    std::uint64_t                               routed{};                 ///< ticks the ingest actor took off the ring
    std::uint32_t                               shards{};                 ///< how many aggregators they were spread across
    std::uint32_t                               quotes{};                 ///< quotes published
    std::uint32_t                               emitted{};                ///< quotes the aggregators say they emitted
    std::uint32_t                               wire_records{};           ///< records the subscriber decoded
    bool                                        wire_complete{};          ///< the subscriber saw the end-of-stream sentinel
    bool                                        subscriber_failed{};      ///< connect failed or the link closed before that sentinel
    std::atomic<bool>                           aggregator_failed{false}; ///< shard received more ticks than ingest announced
    Symbol                                      last_symbol;
    double                                      last_price{};
    double                                      last_vwap{};
    std::uint32_t                               last_updates{};
    std::array<WireSymbolObservation, kSymbols> wire_symbols{};
};

/// The symbol table. Fixed, so `sym_ix` is all the feed thread has to carry.
inline const char *const kSymbolNames[kSymbols] = {"AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META", "TSLA", "AMD"};

struct AuditResult {
    bool        ok;
    const char *reason;
    int         symbol;
};

/// A count-only verdict can pass after an early loss if all downstream counters shrink together.
/// Check the producer's independent reference and the subscriber's final value for each symbol.
inline AuditResult
audit_complete(FeedReference const &feed, Report const &report, std::uint32_t ticks_per_symbol) noexcept {
    if (ticks_per_symbol == 0)
        return {false, "expected tick count is zero", -1};
    if (report.aggregator_failed.load(std::memory_order_acquire))
        return {false, "aggregator did not drain the announced tick count", -1};
    const auto expected = static_cast<std::uint64_t>(kSymbols) * ticks_per_symbol;
    if (!report.wire_complete || report.subscriber_failed)
        return {false, "wire did not finish cleanly", -1};
    if (feed.produced != expected || report.routed != expected)
        return {false, "producer or ingest lost ticks", -1};
    if (report.quotes != expected || report.emitted != expected || report.wire_records != expected || report.latencies.size() != expected)
        return {false, "publisher or wire record count differs from feed", -1};

    for (int i = 0; i < kSymbols; ++i) {
        const auto &source = feed.symbols[i];
        const auto &wire   = report.wire_symbols[i];
        if (source.count != ticks_per_symbol || !wire.seen || wire.records != ticks_per_symbol || wire.updates != ticks_per_symbol)
            return {false, "per-symbol record count differs from feed", i};
        if (wire.last_seq != source.last_seq || wire.last_seq != ticks_per_symbol - 1)
            return {false, "last sequence differs from feed", i};
        if (wire.volume != source.volume)
            return {false, "total volume differs from feed", i};
        if (source.volume == 0)
            return {false, "reference volume is zero", i};
        const auto last_micros = static_cast<std::uint64_t>(source.last_price * 1'000'000.0);
        if (wire.last_micros != last_micros)
            return {false, "last price differs from feed", i};
        const auto vwap_micros = static_cast<std::uint64_t>(source.weighted_sum / static_cast<long double>(source.volume) * 1'000'000.0L);
        const auto delta       = wire.vwap_micros > vwap_micros ? wire.vwap_micros - vwap_micros : vwap_micros - wire.vwap_micros;
        if (delta > 2)
            return {false, "VWAP differs from feed beyond two micro-units", i};
    }
    return {true, "complete", -1};
}

} // namespace market_data
