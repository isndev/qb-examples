/**
 * @file examples/07-applications/03-market-data-hub/src/main.cpp
 * @tier 07-applications
 * @teaches A whole application with NO HTTP and NO SQL: a foreign feed thread bridged by a
 *          lock-free ring, sticky fan-out with WorkerPool, per-shard state, batched publication,
 *          a binary protocol on qb-io, `send<>` on the hot path, `setLatency(0)` — and a
 *          feed-to-publisher-entry latency DISTRIBUTION rather than a headline number.
 * @demonstrates qb::Main, core, setLatency, qb::duration::zero, addActor<T>, start, join,
 *               hasError, usedCoreSet, qb::ActorId,
 *               qb::lockfree::spsc::ringbuffer<Tick, 4096>, enqueue,
 *               qb::io::cout, qb::io::cerr
 * @prerequisites 01-actors/11-hot-path, 04-patterns/03-worker-pool, 02-io/06-framing-toolbox
 * @expect "market-data-hub: "
 * @expect "[feed] a foreign std::thread produced "
 * @expect "[ingest] routed "
 * @expect "[wire] decoded "
 * @expect "[publish] "
 * @expect "[latency] feed-thread timestamp to Publisher handler entry, over "
 * @expect "          A DISTRIBUTION and not a headline: a single number would be a claim about"
 * @expect "=== market-data-hub complete: "
 *
 * WHY THIS PROGRAM EXISTS
 * -----------------------
 * The other two applications in this tier are HTTP + PostgreSQL + Redis CRUD services, and so were
 * the two largest programs in the pre-3.0 corpus. A reader who finishes that corpus has been shown,
 * without anyone saying it, that qb is a web framework. It is not: it is an actor engine whose
 * benchmarks are in nanoseconds, and this program is the shape that argument takes — a pipeline
 * with no request/response anywhere in it.
 *
 * THE PIPELINE, AND WHY EACH HOP IS WHAT IT IS
 * --------------------------------------------
 *   a real std::thread ─spsc ring─▶ IngestActor ─send<>─▶ 3 × AggregatorActor ─push─▶ Publisher
 *                                                                                        │
 *                                                                             binary frames over TCP
 *                                                                                        ▼
 *                                                                                  SubscriberActor
 *
 *   * The FEED is a foreign thread because that is what a market feed is: a library callback, a
 *     kernel bypass NIC, a vendor SDK. `qb::lockfree::spsc::ringbuffer` is the sanctioned bridge —
 *     one producer, one consumer, no mutex, and a full ring returns false instead of growing.
 *   * INGEST routes with `WorkerPool::for_key(symbol)`, not `next()`: a symbol's state lives on
 *     exactly one shard, so sticky routing is what makes lock-free per-symbol aggregation possible.
 *   * The tick event travels by `send<>`. It is unordered, so every tick contributes to
 *     volume/VWAP while only the greatest per-symbol sequence updates the latest price.
 *   * The QUOTE BATCH travels by `push<>`, because it owns a vector. That is not a style choice —
 *     `send` on an event owning heap is a compile error at all three sinks.
 *   * AGGREGATORS batch with `qb::batcher`: 64 quotes or 4 ms, whichever comes first, so the
 *     publisher does one write per batch instead of one per tick.
 *   * The WIRE is a fixed 36-byte record behind a 4-byte length prefix, big-endian, with prices as
 *     integer micro-units and the latest sequence. `size_as_header` does the framing.
 *
 * WHAT THE NUMBER AT THE END IS, AND WHAT IT IS NOT
 * -------------------------------------------------
 * It is the time from the FEED THREAD's timestamp to the start of the Publisher's batch handler:
 * one ring crossing, one cross-core `send`, one aggregation and one batch window. It does NOT
 * include conversion, encoding, socket writes or subscriber reception. The feed runs flat out,
 * so the ring is usually non-empty and this number includes queueing delay.
 * It is printed as min/p50/p90/p99/max BECAUSE a single headline number would be a claim about
 * hardware this program has never run on — and because the batch WINDOW (up to 4 ms) dominates the
 * tail by construction, so a p99 that looks bad is usually a batch that was waiting on purpose.
 * Compare the shape of the distribution between two builds on ONE machine; never compare medians
 * between machines. The independent producer reference and subscriber's per-symbol final
 * records, not the latency samples, check completeness and final values. Intermediate
 * prices/VWAPs are not compared to an independent reference.
 *
 * WHERE THE PROMISE LIVES, AND WHY IT IS NARROWER THAN THE PROGRAM
 * ----------------------------------------------------------------
 * `@demonstrates` lists what THIS FILE contains, because that is what the header guard can check:
 * it reads a file, not a translation unit, and a project's claim must be provable in the source
 * that carries it. The pipeline's own vocabulary — `WorkerPool::for_key`, `qb::batcher`, `send<>`,
 * `size_as_header`, the endian helpers — lives in `src/actors.h` and
 * `include/market_data/protocol.h`, each documented at the top of its own file. `main()` owns the
 * wiring and THE REPORT: every line this program promises to print is printed here, from a
 * `Report` the actors filled in.
 *
 * SHUTDOWN IS A SEQUENCE, NOT AN EXIT
 * -----------------------------------
 * The feed thread stops, ingest sees an empty ring and a finished producer and pushes `EndOfFeed`
 * with each shard's expected count. A shard waits for all its unordered ticks, failing after
 * 20 seconds if one never arrives, before its FINAL manual `flush()` —
 * the batcher's window timer is cancelled with the actor, so buffered items would otherwise be
 * dropped — the publisher writes an empty-symbol sentinel record, and only when the SUBSCRIBER has
 * decoded it does the successful path call `qb::Main::stop()`. A failed connect or a disconnect
 * before that sentinel stops the engine with an explicit failed verdict. Nothing sleeps and hopes.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-applications-market-data-hub
 * Run:
 *   ./build/presets/release/examples/07-applications/03-market-data-hub/qb-example-applications-market-data-hub
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include <qb/main.h>
#include <qb/system/lockfree/spsc.h>

#include "actors.h"

using namespace market_data;

namespace {

/// The port the hub publishes on. Fixed rather than ephemeral because the subscriber is a separate
/// actor that must reach it, and because a market feed has a well-known endpoint.
constexpr std::uint16_t kPort = 18432;

/// The foreign thread. It knows nothing about actors, cores or events — it produces `Tick`s into a
/// bounded ring, exactly as a vendor SDK callback would.
void
feed_thread(TickRing &ring, std::atomic<bool> &feeding, std::atomic<bool> &publisher_accepted, std::atomic<bool> &subscriber_started,
            std::atomic<bool> &stop_requested, FeedReference *reference) {
    // Wait for both ends of the local wire before producing anything. Without the publisher's
    // accept, the first ~10000 quotes are sent into an empty session list; without the
    // subscriber's start, the receive side may not yet be draining its socket.
    while (!publisher_accepted.load(std::memory_order_acquire) || !subscriber_started.load(std::memory_order_acquire)) {
        if (stop_requested.load(std::memory_order_acquire)) {
            feeding.store(false, std::memory_order_release);
            return;
        }
        std::this_thread::yield();
    }

    std::mt19937                           rng(12345); // fixed seed: the run is reproducible
    std::uniform_real_distribution<double> drift(-0.5, 0.5);
    std::uniform_int_distribution<int>     lots(1, 500);
    double                                 price[kSymbols];
    for (int s = 0; s < kSymbols; ++s)
        price[s] = 100.0 + 25.0 * s;

    std::uint64_t produced = 0;
    for (int i = 0; i < kTicksPerSymbol; ++i) {
        for (int s = 0; s < kSymbols; ++s) {
            price[s] = std::max(1.0, price[s] + drift(rng));
            Tick t{now_ns(), static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(s), price[s], static_cast<std::uint32_t>(lots(rng))};
            // A full ring returns false. Yielding is the honest thing for a producer that cannot
            // drop: growing the queue would only move the failure somewhere less visible.
            while (!ring.enqueue(t)) {
                // A stopped engine cannot drain the ring. Only check the stop flag on this
                // backpressure path; successful enqueues keep the ordinary tick path unchanged.
                if (stop_requested.load(std::memory_order_acquire)) {
                    feeding.store(false, std::memory_order_release);
                    return;
                }
                std::this_thread::yield();
            }
            if (reference)
                reference->record(t);
            ++produced;
        }
    }
    qb::io::cout() << "[feed] a foreign std::thread produced " << produced << " ticks into a lock-free spsc ring\n";
    feeding.store(false, std::memory_order_release);
}

} // namespace

#ifndef QB_MARKET_DATA_FEED_TEST
int
main() {
    // Spelled out rather than through the `TickRing` alias: the capacity is part of the design
    // (a full ring makes the producer yield, which is the back-pressure this program wants to
    // show) and the type is what the guard can check this file for.
    qb::lockfree::spsc::ringbuffer<Tick, 4096> ring;
    std::atomic<bool>                          feeding{true};
    std::atomic<bool>                          publisher_accepted{false};
    std::atomic<bool>                          subscriber_started{false};
    std::atomic<bool>                          stop_feed{false};
    Report                                     report;
    FeedReference                              reference;

    qb::Main engine;

    // setLatency(0) on every core this program uses: the loop never parks, it spins. That is the
    // right setting for a latency-sensitive pipeline and the WRONG one for anything else — it
    // burns a core per VirtualCore whether or not there is work. Set it deliberately, per core.
    for (qb::CoreId c = 0; c <= static_cast<qb::CoreId>(kAggregators) + 1; ++c)
        engine.core(c).setLatency(qb::duration::zero());

    // Core 0 publishes and serves the socket; cores 1..N aggregate; the last core ingests. The
    // placement is explicit because a pipeline's whole point is that its stages do not share a
    // thread.
    qb::ActorId publisher = engine.addActor<PublisherActor>(0, kPort, &publisher_accepted, &report);

    std::vector<qb::ActorId> shards;
    shards.reserve(kAggregators);
    for (int i = 0; i < kAggregators; ++i)
        shards.push_back(engine.addActor<AggregatorActor>(static_cast<qb::CoreId>(1 + i), publisher, &report));

    engine.addActor<IngestActor>(static_cast<qb::CoreId>(kAggregators + 1), &ring, &feeding, shards, &report);
    // The private test probe sends only the subscriber to a reserved adjacent port. The
    // publisher still binds kPort, so its successful startup cannot mask a failed connect.
    const std::uint16_t subscriber_port = std::getenv("QB_MARKET_DATA_TEST_SUBSCRIBER_PORT") ? kPort + 1 : kPort;
    engine.addActor<SubscriberActor>(0, publisher, subscriber_port, &subscriber_started, &report);

    qb::io::cout() << "market-data-hub: " << kSymbols << " symbols, " << kTotalTicks << " ticks, " << kAggregators << " shards, on "
                   << engine.usedCoreSet().size() << " cores\n\n";

    // `start()` returns after the core startup barrier. Launch the feed only if every actor
    // initialized: a failed publisher never sets subscriber_ready, so a thread started after
    // that failure would wait forever. On the normal path, launch AFTER startup so feed
    // timestamps do not include engine initialization.
    engine.start();

    std::thread feed;
    if (!engine.hasError())
        feed = std::thread(feed_thread, std::ref(ring), std::ref(feeding), std::ref(publisher_accepted), std::ref(subscriber_started),
                           std::ref(stop_feed), &reference);
    else
        feeding.store(false, std::memory_order_release);

    // Returns on failed startup or when the publisher calls qb::Main::stop().
    engine.join();
    stop_feed.store(true, std::memory_order_release); // release either wait if the engine stopped early
    if (feed.joinable())
        feed.join();

    // The exit code is the engine's own verdict. A pipeline that failed to start must not report
    // success — the publisher's onInit returns false when its bind fails, and that lands here.
    // ---- the report, printed by the file that promises it ------------------------------------
    const auto pct = [&report](double p) -> std::uint64_t {
        if (report.latencies.empty())
            return 0;
        return report.latencies[static_cast<std::size_t>(p * static_cast<double>(report.latencies.size() - 1))];
    };

    qb::io::cout() << "[ingest] routed " << report.routed << " ticks from a foreign thread across " << report.shards
                   << " shards, with no mutex and no allocation on the path\n";
    qb::io::cout() << "[wire] decoded " << report.wire_records << " binary records; the last quote was " << report.last_symbol.c_str()
                   << " last=" << report.last_price << " vwap=" << report.last_vwap << " after " << report.last_updates << " updates\n";
    qb::io::cout() << "[publish] " << report.quotes << " quotes published in binary frames; the aggregators emitted " << report.emitted << "\n";
    qb::io::cout() << "[latency] feed-thread timestamp to Publisher handler entry, over " << report.latencies.size()
                   << " quotes (nanoseconds, on THIS machine and this build):\n"
                   << "          min " << (report.latencies.empty() ? 0 : report.latencies.front()) << "  p50 " << pct(0.50) << "  p90 "
                   << pct(0.90) << "  p99 " << pct(0.99) << "  max " << (report.latencies.empty() ? 0 : report.latencies.back()) << "\n";
    qb::io::cout() << "          A DISTRIBUTION and not a headline: a single number would be a claim about\n"
                      "          somebody else's hardware. Compare shapes across runs, not medians across machines\n";

    // Compare against the foreign producer, not only against downstream counts that could all
    // shrink together. The reference is read only after its thread has joined.
    const AuditResult audit = audit_complete(reference, report, kTicksPerSymbol);
    const bool        ok    = !engine.hasError() && audit.ok;
    if (!ok)
        qb::io::cerr() << "[fatal] engine error=" << (engine.hasError() ? "yes" : "no")
                       << ", subscriber finished=" << (report.wire_complete ? "yes" : "no")
                       << ", subscriber failed=" << (report.subscriber_failed ? "yes" : "no") << ", quotes=" << report.quotes
                       << ", emitted=" << report.emitted << ", decoded=" << report.wire_records << ", audit=" << audit.reason
                       << ", symbol=" << audit.symbol << "\n";
    qb::io::cout() << "\n=== market-data-hub complete: "
                   << (ok ? "feed, fan-out, batch, wire — all tick counts and final symbol values verified"
                          : "FAILED — engine, feed, or wire oracle rejected the run")
                   << " ===\n";
    return ok ? 0 : 1;
}
#endif
