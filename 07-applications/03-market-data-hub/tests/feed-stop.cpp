#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <gtest/gtest.h>

// Exercise the producer compiled by the example itself, without opening a socket or
// duplicating its wait loops. Only main() is excluded from this test translation unit.
#define QB_MARKET_DATA_FEED_TEST
#include "../src/main.cpp"

namespace market_feed_stop_test {

void
join_or_fail(std::thread &producer, std::atomic<bool> &feeding) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (feeding.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    if (feeding.load(std::memory_order_acquire)) {
        std::cerr << "feed did not leave its wait within 2 s\n";
        std::_Exit(2); // avoid std::thread's destructor terminating an otherwise informative test
    }
    producer.join();
}

TEST(MarketFeedStop, WaitingForWire) {
    TickRing          ring;
    std::atomic<bool> feeding{true};
    std::atomic<bool> accepted{false};
    std::atomic<bool> started{false};
    std::atomic<bool> stop{false};

    std::thread producer(feed_thread, std::ref(ring), std::ref(feeding), std::ref(accepted), std::ref(started), std::ref(stop), nullptr);
    stop.store(true, std::memory_order_release);
    join_or_fail(producer, feeding);
    EXPECT_TRUE(ring.empty());
}

TEST(MarketFeedStop, FullRing) {
    TickRing          ring;
    std::atomic<bool> feeding{true};
    std::atomic<bool> accepted{true};
    std::atomic<bool> started{true};
    std::atomic<bool> stop{false};

    std::thread producer(feed_thread, std::ref(ring), std::ref(feeding), std::ref(accepted), std::ref(started), std::ref(stop), nullptr);
    stop.store(true, std::memory_order_release);
    join_or_fail(producer, feeding);

    Tick        tick;
    std::size_t queued = 0;
    while (ring.dequeue(&tick))
        ++queued;
    EXPECT_EQ(queued, 4096u) << "the producer must reach the full ring before its stop check";
}

TEST(MarketFeedStop, DelayedOlderTickCannotReplaceLatestPrice) {
    SymbolBook book;
    book.accept(11, 111.0, 10); // Direct path arrives before an older queued tick.
    book.accept(10, 100.0, 5);  // The delayed path still contributes to aggregates.

    EXPECT_EQ(book.last_seq, 11u);
    EXPECT_DOUBLE_EQ(book.last, 111.0);
    EXPECT_EQ(book.updates, 2u);
    EXPECT_EQ(book.volume, 15u);
    EXPECT_EQ(book.vwap_den, 15u);
    EXPECT_DOUBLE_EQ(book.vwap_num / book.vwap_den, (111.0 * 10 + 100.0 * 5) / 15);
}

TEST(MarketFeedStop, EndMarkerWaitsForDelayedTicks) {
    ShardDrain drain;
    const auto start = std::chrono::steady_clock::time_point{};
    drain.end(2, start); // A terminal event may reach this shard before two unordered sends.
    EXPECT_FALSE(drain.complete());
    EXPECT_FALSE(drain.expired(start + std::chrono::seconds(19)));
    EXPECT_TRUE(drain.expired(start + std::chrono::seconds(20)));
    drain.tick();
    EXPECT_FALSE(drain.complete());
    drain.tick();
    EXPECT_TRUE(drain.complete());
    drain.tick();
    EXPECT_TRUE(drain.overrun());
}

class DrainTriggerActor final : public qb::Actor {
    qb::ActorId _aggregator;

public:
    explicit DrainTriggerActor(qb::ActorId aggregator)
        : _aggregator(aggregator) {}

    qb::io::async::task<bool>
    onInit() final {
        push<EndOfFeed>(_aggregator).expected = 1;
        co_return true;
    }
};

TEST(MarketFeedStop, MissingTickStopsRealAggregator) {
    Report     report;
    qb::Main   engine;
    const auto aggregator = engine.addActor<AggregatorActor>(0, qb::ActorId{}, &report, std::chrono::milliseconds(25));
    engine.addActor<DrainTriggerActor>(0, aggregator);
    engine.start();
    engine.join();

    EXPECT_TRUE(report.aggregator_failed.load(std::memory_order_acquire));
    EXPECT_FALSE(audit_complete(FeedReference{}, report, 1).ok);
}

TEST(MarketFeedStop, WireRecordCarriesLatestSequence) {
    WireQuote quote;
    std::memcpy(quote.symbol, "AAPL", 4);
    quote.last_micros = 12'345'678;
    quote.vwap_micros = 12'000'000;
    quote.volume      = 99;
    quote.updates     = 2;
    quote.last_seq    = 0x01020304;

    std::string bytes;
    encode(quote, bytes);
    ASSERT_EQ(bytes.size(), kWireQuoteSize);
    EXPECT_EQ(static_cast<unsigned char>(bytes[32]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(bytes[33]), 0x02);
    EXPECT_EQ(static_cast<unsigned char>(bytes[34]), 0x03);
    EXPECT_EQ(static_cast<unsigned char>(bytes[35]), 0x04);
    const WireQuote decoded = decode(bytes.data());
    EXPECT_EQ(decoded.last_micros, quote.last_micros);
    EXPECT_EQ(decoded.vwap_micros, quote.vwap_micros);
    EXPECT_EQ(decoded.volume, quote.volume);
    EXPECT_EQ(decoded.updates, quote.updates);
    EXPECT_EQ(decoded.last_seq, quote.last_seq);
}

TEST(MarketFeedStop, SentinelMustBeEntirelyEmpty) {
    WireQuote marker;
    EXPECT_TRUE(is_end_marker(marker));
    marker.symbol[1] = 'X';
    EXPECT_FALSE(is_end_marker(marker));
    marker        = WireQuote{};
    marker.volume = 1;
    EXPECT_FALSE(is_end_marker(marker));
}

TEST(MarketFeedStop, OracleRejectsEarlyLossAndCorruptedFinalValue) {
    FeedReference reference;
    Report        report;
    for (int symbol = 0; symbol < kSymbols; ++symbol) {
        const double first = 100.0 + symbol;
        const double last  = first + 1.0;
        reference.record(Tick{0, 0, static_cast<std::uint32_t>(symbol), first, 10});
        reference.record(Tick{0, 1, static_cast<std::uint32_t>(symbol), last, 20});

        auto &observed       = report.wire_symbols[static_cast<std::size_t>(symbol)];
        observed.seen        = true;
        observed.records     = 2;
        observed.updates     = 2;
        observed.last_seq    = 1;
        observed.volume      = 30;
        observed.last_micros = static_cast<std::uint64_t>(last * 1'000'000.0);
        observed.vwap_micros = static_cast<std::uint64_t>((first * 10.0 + last * 20.0) / 30.0 * 1'000'000.0);
    }
    report.routed        = reference.produced;
    report.quotes        = static_cast<std::uint32_t>(reference.produced);
    report.emitted       = report.quotes;
    report.wire_records  = report.quotes;
    report.wire_complete = true;
    report.latencies.resize(reference.produced);
    ASSERT_TRUE(audit_complete(reference, report, 2).ok);

    --report.routed; // Old downstream-only equality still passes this loss.
    EXPECT_FALSE(audit_complete(reference, report, 2).ok);
    ++report.routed;
    report.wire_symbols[2].last_micros += 100;
    EXPECT_FALSE(audit_complete(reference, report, 2).ok);
    report.wire_symbols[2].last_micros -= 100;
    report.wire_symbols[2].last_seq = 0;
    EXPECT_FALSE(audit_complete(reference, report, 2).ok);
}

} // namespace market_feed_stop_test
