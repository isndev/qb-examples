/**
 * @file examples/07-applications/03-market-data-hub/src/main.cpp
 * @tier 07-applications
 * @teaches A whole application with NO HTTP and NO SQL: a foreign feed thread bridged by a
 *          lock-free ring, sticky fan-out with WorkerPool, per-shard state, batched publication,
 *          a binary protocol on qb-io, `send<>` on the hot path, `setLatency(0)` — and an
 *          end-to-end latency DISTRIBUTION rather than a headline number.
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
 * @expect "[latency] feed-thread timestamp to wire, over "
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
 *   * The tick event travels by `send<>`. It is unordered and legal only for a trivially-
 *     destructible event; ticks are independent samples, so ordering between them buys nothing and
 *     the destructor bookkeeping `push<>` does is pure cost.
 *   * The QUOTE BATCH travels by `push<>`, because it owns a vector. That is not a style choice —
 *     `send` on an event owning heap is a compile error at all three sinks.
 *   * AGGREGATORS batch with `qb::batcher`: 64 quotes or 4 ms, whichever comes first, so the
 *     publisher does one write per batch instead of one per tick.
 *   * The WIRE is a fixed 32-byte record behind a 4-byte length prefix, big-endian, with prices as
 *     integer micro-units. `size_as_header` does the framing; no parser is written.
 *
 * WHAT THE NUMBER AT THE END IS, AND WHAT IT IS NOT
 * -------------------------------------------------
 * It is the time from the FEED THREAD's timestamp to the moment the quote is handed to the socket:
 * one ring crossing, one cross-core `send`, one aggregation, one batch window and one serialisation.
 * The feed runs FLAT OUT, so the ring is usually non-empty and this number includes QUEUEING delay —
 * which is exactly what a real feed's end-to-end latency includes, and the reason a burst test and a
 * paced test measure different things.
 * It is printed as min/p50/p90/p99/max BECAUSE a single headline number would be a claim about
 * hardware this program has never run on — and because the batch WINDOW (up to 4 ms) dominates the
 * tail by construction, so a p99 that looks bad is usually a batch that was waiting on purpose.
 * Compare the shape of the distribution between two builds on ONE machine; never compare medians
 * between machines.
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
 * (ordered, so it lands after the last `send<>`), each aggregator does a FINAL manual `flush()` —
 * the batcher's window timer is cancelled with the actor, so buffered items would otherwise be
 * dropped — the publisher writes an empty-symbol sentinel record, and only when the SUBSCRIBER has
 * decoded it does anything call `qb::Main::stop()`. Nothing here sleeps and hopes.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-applications-market-data-hub
 * Run:
 *   ./build/presets/release/examples/07-applications/03-market-data-hub/qb-example-applications-market-data-hub
 */

#include <atomic>
#include <chrono>
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
feed_thread(TickRing &ring, std::atomic<bool> &feeding, std::atomic<bool> &subscriber_ready) {
    // Wait for the wire's other end before producing anything. Without this the first ~10000
    // quotes are published into an empty session list — measured — and the record count at the
    // end becomes a number about startup timing rather than about the pipeline.
    while (!subscriber_ready.load(std::memory_order_acquire))
        std::this_thread::yield();

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
            while (!ring.enqueue(t))
                std::this_thread::yield();
            ++produced;
        }
    }
    qb::io::cout() << "[feed] a foreign std::thread produced " << produced << " ticks into a lock-free spsc ring\n";
    feeding.store(false, std::memory_order_release);
}

} // namespace

int
main() {
    // Spelled out rather than through the `TickRing` alias: the capacity is part of the design
    // (a full ring makes the producer yield, which is the back-pressure this program wants to
    // show) and the type is what the guard can check this file for.
    qb::lockfree::spsc::ringbuffer<Tick, 4096> ring;
    std::atomic<bool>                          feeding{true};
    std::atomic<bool>                          subscriber_ready{false};
    Report                                     report;

    qb::Main engine;

    // setLatency(0) on every core this program uses: the loop never parks, it spins. That is the
    // right setting for a latency-sensitive pipeline and the WRONG one for anything else — it
    // burns a core per VirtualCore whether or not there is work. Set it deliberately, per core.
    for (qb::CoreId c = 0; c <= static_cast<qb::CoreId>(kAggregators) + 1; ++c)
        engine.core(c).setLatency(qb::duration::zero());

    // Core 0 publishes and serves the socket; cores 1..N aggregate; the last core ingests. The
    // placement is explicit because a pipeline's whole point is that its stages do not share a
    // thread.
    qb::ActorId publisher = engine.addActor<PublisherActor>(0, kPort, &subscriber_ready, &report);

    std::vector<qb::ActorId> shards;
    shards.reserve(kAggregators);
    for (int i = 0; i < kAggregators; ++i)
        shards.push_back(engine.addActor<AggregatorActor>(static_cast<qb::CoreId>(1 + i), publisher));

    engine.addActor<IngestActor>(static_cast<qb::CoreId>(kAggregators + 1), &ring, &feeding, shards, &report);
    engine.addActor<SubscriberActor>(0, publisher, kPort, &report);

    qb::io::cout() << "market-data-hub: " << kSymbols << " symbols, " << kTotalTicks << " ticks, " << kAggregators << " shards, on "
                   << engine.usedCoreSet().size() << " cores\n\n";

    // `start()` is ASYNC (that is its default) and returns as soon as the cores are up. The feed
    // thread is launched AFTER it on purpose: its timestamps are the start of every latency
    // measurement below, and starting it first would charge engine startup to the first ticks.
    engine.start();

    std::thread feed(feed_thread, std::ref(ring), std::ref(feeding), std::ref(subscriber_ready));
    engine.join(); // returns when the publisher calls qb::Main::stop()
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
    qb::io::cout() << "[latency] feed-thread timestamp to wire, over " << report.latencies.size()
                   << " quotes (nanoseconds, on THIS machine and this build):\n"
                   << "          min " << (report.latencies.empty() ? 0 : report.latencies.front()) << "  p50 " << pct(0.50) << "  p90 "
                   << pct(0.90) << "  p99 " << pct(0.99) << "  max " << (report.latencies.empty() ? 0 : report.latencies.back()) << "\n";
    qb::io::cout() << "          A DISTRIBUTION and not a headline: a single number would be a claim about\n"
                      "          somebody else's hardware. Compare shapes across runs, not medians across machines\n";

    // The pipeline's own verdict, and the only one worth exiting on: every quote the aggregators
    // emitted reached the wire and was decoded there. A run that loses a record must not exit 0
    // just because the engine had no error of its own.
    const bool complete = report.quotes > 0 && report.quotes == report.emitted && report.wire_records == report.quotes;
    const bool ok       = !engine.hasError() && complete;
    if (!ok)
        qb::io::cerr() << "[fatal] engine error=" << (engine.hasError() ? "yes" : "no") << ", quotes=" << report.quotes
                       << ", emitted=" << report.emitted << ", decoded=" << report.wire_records << "\n";
    qb::io::cout() << "\n=== market-data-hub complete: "
                   << (ok ? "feed, fan-out, batch, wire — every quote emitted was decoded at the other end"
                          : "FAILED — the engine errored, or a quote did not reach the wire")
                   << " ===\n";
    return ok ? 0 : 1;
}
