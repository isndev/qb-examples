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

    std::thread producer(feed_thread, std::ref(ring), std::ref(feeding), std::ref(accepted), std::ref(started), std::ref(stop));
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

    std::thread producer(feed_thread, std::ref(ring), std::ref(feeding), std::ref(accepted), std::ref(started), std::ref(stop));
    stop.store(true, std::memory_order_release);
    join_or_fail(producer, feeding);

    Tick        tick;
    std::size_t queued = 0;
    while (ring.dequeue(&tick))
        ++queued;
    EXPECT_EQ(queued, 4096u) << "the producer must reach the full ring before its stop check";
}

} // namespace market_feed_stop_test
