/*
 * market-data-hub — the four actors, and the one decision each of them makes.
 *
 *   IngestActor      drains a lock-free ring written by a FOREIGN thread and routes each tick to
 *                    a shard with `WorkerPool::for_key`. Hot path: `send<>`, not `push<>`.
 *   AggregatorActor  keeps per-symbol state and COALESCES its output with `qb::batcher`.
 *   PublisherActor   owns the TCP server, writes batches as binary frames, and measures feed
 *                    timestamp to Publisher handler entry for every quote.
 *   SubscriberActor  the wire's other end, in this same process, so the format is proven rather
 *                    than asserted.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/main.h>
#include <qb/system/lockfree/spsc.h>
#include <qb/core/patterns/aggregate.h>
#include <qb/core/patterns/routing.h>

#include "market_data/model.h"
#include "market_data/protocol.h"

namespace market_data {

using TickRing = qb::lockfree::spsc::ringbuffer<Tick, 4096>;

inline std::uint64_t
now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------------------
// INGEST — the only actor that touches the foreign thread's ring.
// ---------------------------------------------------------------------------------------
class IngestActor
    : public qb::Actor
    , public qb::ICallback {
    TickRing                  *_ring;
    std::atomic<bool>         *_feeding;
    std::vector<qb::ActorId>   _shard_ids;
    std::vector<std::uint32_t> _sent_by_shard;
    qb::WorkerPool             _shards;
    Report                    *_report = nullptr;
    std::uint64_t              _routed = 0;
    bool                       _ended  = false;

public:
    IngestActor(TickRing *ring, std::atomic<bool> *feeding, std::vector<qb::ActorId> shards, Report *report)
        : _ring(ring)
        , _feeding(feeding)
        , _shard_ids(std::move(shards))
        , _sent_by_shard(_shard_ids.size(), 0)
        , _shards(_shard_ids)
        , _report(report) {}

    qb::io::async::task<bool>
    onInit() override {
        registerCallback(*this); // one drain per loop turn
        co_return true;
    }

    void
    on(qb::LoopEvent const &) override {
        // Drain what the feed thread has produced since the last turn. `dequeue` returns false on
        // an empty ring rather than blocking: back-pressure is the producer's problem, which is
        // the honest design for a bounded queue.
        Tick t;
        while (_ring->dequeue(&t)) {
            // `for_key` is `workers[k % size()]`: the SAME symbol always reaches the SAME shard,
            // which is what lets an aggregator keep per-symbol state with no locking and no
            // cross-core lookup. Stable only while the pool's size is.
            const qb::ActorId dest = _shards.for_key(t.sym_ix);

            // THE HOT PATH, and the one line worth staring at. `send<>` is unordered and legal
            // for this trivially-destructible event. Every tick contributes to volume and VWAP;
            // the aggregator compares per-symbol sequence numbers before replacing the latest
            // price, so a delayed older tick cannot roll it back.
            // Note the shape: `send` CONSTRUCTS the event from these arguments and returns void,
            // where `push` returns a reference you fill in afterwards. There is no reference to
            // hold here, which is also why there is nothing to invalidate.
            send<TickEvent>(dest, Symbol(kSymbolNames[t.sym_ix]), t.at_ns, t.seq, t.price, t.volume);
            ++_sent_by_shard[t.sym_ix % _shard_ids.size()];
            ++_routed;
        }

        if (!_ended && !_feeding->load(std::memory_order_acquire) && _ring->empty()) {
            _ended          = true;
            _report->routed = _routed;
            _report->shards = static_cast<std::uint32_t>(_shards.size());
            for (std::size_t shard = 0; shard < _shard_ids.size(); ++shard)
                push<EndOfFeed>(_shard_ids[shard]).expected = _sent_by_shard[shard];
            unregisterCallback(*this);
        }
    }
};

// ---------------------------------------------------------------------------------------
// AGGREGATOR — per-symbol state, and one publish per batch instead of one per tick.
// ---------------------------------------------------------------------------------------
class AggregatorActor
    : public qb::Actor
    , public qb::ICallback {
    qb::ActorId                                _publisher;
    Report                                    *_report;
    qb::unordered_map<std::string, SymbolBook> _books;
    std::uint32_t                              _emitted = 0;
    ShardDrain                                 _drain;
    bool                                       _finalized      = false;
    bool                                       _watching_drain = false;
    std::chrono::steady_clock::duration        _drain_timeout;

    // Held as a MEMBER, and `[this]` is safe here precisely because it is: the window timer is
    // bound to this actor's scope, so a flush can never fire after the actor is gone.
    qb::batcher<Quote> _batch{kBatchMax, qb::duration{std::chrono::milliseconds(4)}, [this](std::vector<Quote> &&quotes) {
                                  publish(std::move(quotes));
                              }};

public:
    AggregatorActor(qb::ActorId publisher, Report *report, std::chrono::steady_clock::duration drain_timeout = kShardDrainTimeout)
        : _publisher(publisher)
        , _report(report)
        , _drain_timeout(drain_timeout) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<TickEvent>(*this);
        registerEvent<EndOfFeed>(*this);
        co_return true;
    }

    void
    on(TickEvent &t) {
        SymbolBook &b = _books[std::string(t.symbol.c_str())];
        b.accept(t.seq, t.price, t.volume);

        Quote q;
        q.symbol   = t.symbol;
        q.at_ns    = t.at_ns; // carried through to the Publisher handler-entry measurement
        q.last     = b.last;
        q.vwap     = b.vwap_den ? b.vwap_num / static_cast<double>(b.vwap_den) : b.last;
        q.volume   = b.volume;
        q.updates  = b.updates;
        q.last_seq = b.last_seq;

        // May flush RIGHT HERE if this quote fills the buffer.
        _batch.add(context(), std::move(q));
        _drain.tick();
        finish_if_ready();
    }

    void
    on(EndOfFeed const &end) {
        _drain.end(end.expected, std::chrono::steady_clock::now(), _drain_timeout);
        if (!_drain.complete() && !_watching_drain) {
            registerCallback(*this);
            _watching_drain = true;
        }
        finish_if_ready();
    }

    void
    on(qb::LoopEvent const &) override {
        if (_watching_drain && _drain.expired(std::chrono::steady_clock::now()))
            fail_drain("[aggregator] timed out waiting for announced ticks");
    }

private:
    void
    stop_watching_drain() {
        if (_watching_drain) {
            unregisterCallback(*this);
            _watching_drain = false;
        }
    }

    void
    fail_drain(char const *reason) {
        stop_watching_drain();
        _report->aggregator_failed.store(true, std::memory_order_release);
        qb::io::cerr() << reason << '\n';
        qb::Main::stop();
    }

    void
    finish_if_ready() {
        if (_drain.overrun()) {
            fail_drain("[aggregator] received more ticks than ingest announced");
            return;
        }
        if (_finalized || !_drain.complete())
            return;
        _finalized = true;
        stop_watching_drain();
        // The batcher's timer is cancelled with this actor. Flush after every announced tick,
        // including sends that reached us after EndOfFeed.
        _batch.flush();
        push<AggregatorDone>(_publisher).emitted = _emitted;
    }

    void
    publish(std::vector<Quote> &&quotes) {
        _emitted += static_cast<std::uint32_t>(quotes.size());
        auto &ev  = push<QuoteBatch>(_publisher); // OWNS HEAP -> push, never send
        ev.quotes = std::move(quotes);
    }
};

// ---------------------------------------------------------------------------------------
// PUBLISHER — the wire, and the measurement.
// ---------------------------------------------------------------------------------------
class PublisherActor;

class FeedSession : public qb::io::use<FeedSession>::tcp::client<PublisherActor> {
public:
    using Protocol = QuoteFeed<FeedSession>;
    explicit FeedSession(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&) {} // subscribers never speak
};

class PublisherActor
    : public qb::Actor
    , public qb::io::use<PublisherActor>::tcp::server<FeedSession> {
    std::vector<std::uint64_t> _latencies; ///< one sample per published quote, in nanoseconds
    std::uint32_t              _quotes   = 0;
    int                        _done     = 0;
    std::uint32_t              _expected = 0;
    bool                       _sent_end = false;
    std::uint16_t              _port     = 0;
    std::atomic<bool>         *_ready    = nullptr;
    Report                    *_report   = nullptr;

public:
    PublisherActor(std::uint16_t port, std::atomic<bool> *subscriber_ready, Report *report)
        : _port(port)
        , _ready(subscriber_ready)
        , _report(report) {
        _latencies.reserve(static_cast<std::size_t>(kTotalTicks));
    }

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<QuoteBatch>(*this);
        registerEvent<AggregatorDone>(*this);
        registerEvent<SubscriberDone>(*this);
        if (transport().listen_v4(_port, "127.0.0.1") != 0) {
            qb::io::cerr() << "[publisher] could not bind 127.0.0.1:" << _port << "\n";
            co_return false; // a failed bind must not look like a healthy start
        }
        start();
        co_return true;
    }

    /// A subscriber has attached. This is one of the feed's two readiness gates: publishing
    /// into an empty session list would send those quotes nowhere and make the count a lie.
    void
    on(IOSession &) {
        _ready->store(true, std::memory_order_release);
    }

    void
    on(QuoteBatch &batch) {
        std::string frame;
        frame.reserve(batch.quotes.size() * kWireQuoteSize);
        const std::uint64_t at = now_ns();
        for (auto const &q : batch.quotes) {
            _latencies.push_back(at - q.at_ns);
            WireQuote w;
            std::memcpy(w.symbol, q.symbol.c_str(), std::min<std::size_t>(sizeof(w.symbol), q.symbol.size()));
            w.last_micros = static_cast<std::uint64_t>(q.last * 1'000'000.0);
            w.vwap_micros = static_cast<std::uint64_t>(q.vwap * 1'000'000.0);
            w.volume      = q.volume;
            w.updates     = q.updates;
            w.last_seq    = q.last_seq;
            encode(w, frame);
        }
        _quotes += static_cast<std::uint32_t>(batch.quotes.size());
        broadcast_frame(frame);
    }

    void
    on(AggregatorDone const &d) {
        _expected += d.emitted;
        if (++_done < kAggregators || _sent_end)
            return;
        // Every shard has flushed. One empty-symbol record says so on the wire.
        _sent_end = true;
        std::string frame;
        encode(WireQuote{}, frame);
        broadcast_frame(frame);
    }

    void
    on(SubscriberDone const &s) {
        // Fill the report and stop; main() prints it. Everything worth asserting about this run is
        // therefore printed by the file that promises it.
        std::sort(_latencies.begin(), _latencies.end());
        _report->latencies    = std::move(_latencies);
        _report->quotes       = _quotes;
        _report->emitted      = _expected;
        _report->wire_records = s.records;
        qb::Main::stop();
    }

private:
    void
    broadcast_frame(std::string const &payload) {
        const std::string frame = framed<FeedSession::Protocol>(payload);
        for (auto &[id, session] : sessions())
            *session << frame;
    }
};

// ---------------------------------------------------------------------------------------
// SUBSCRIBER — the other end of the wire, in this process.
// ---------------------------------------------------------------------------------------
struct SubscriberSocketReady : qb::Event {
    std::shared_ptr<qb::io::tcp::socket> socket;

    explicit SubscriberSocketReady(std::shared_ptr<qb::io::tcp::socket> connected)
        : socket(std::move(connected)) {}
};

inline void
fail_subscriber(Report &report, const char *reason) {
    if (report.wire_complete || report.subscriber_failed)
        return; // late disconnect after the sentinel, or failure already reported
    report.subscriber_failed = true;
    qb::io::cerr() << reason << '\n';
    qb::Main::stop();
}

class SubscriberActor
    : public qb::Actor
    , public qb::io::use<SubscriberActor>::tcp::client<> {
    qb::ActorId        _publisher;
    std::uint16_t      _port;
    std::atomic<bool> *_started = nullptr;
    Report            *_report  = nullptr;
    std::uint32_t      _records = 0;
    WireQuote          _last{};

public:
    using Protocol = QuoteFeed<SubscriberActor>;

    SubscriberActor(qb::ActorId publisher, std::uint16_t port, std::atomic<bool> *started, Report *report)
        : _publisher(publisher)
        , _port(port)
        , _started(started)
        , _report(report) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<SubscriberSocketReady>(*this);
        // A direct co_await of tcp::connect in onInit now resumes (qb's
        // InitIoAwaiters.ConnectInInitResumes tests that first-suspension path). The
        // observation before the 2026-08-16 connector fix is no longer a usage rule.
        // Keep spawn here so onInit returns before the socket exists. SubscriberSocketReady
        // installs and starts it; the feed waits for that start and the publisher's accept.
        const auto  port   = _port;
        auto *const report = _report; // main owns it until every core and the feed have joined
        spawn([port, report](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            auto sock = co_await qb::io::async::tcp::connect<qb::io::transport::tcp>(qb::io::uri("tcp://127.0.0.1:" + std::to_string(port)),
                                                                                     std::chrono::seconds(2));
            if (ctx.cancelled())
                co_return;
            if (!sock.has_value()) {
                fail_subscriber(*report, "[subscriber] could not connect to the feed");
                co_return;
            }
            ctx.push<SubscriberSocketReady>(std::make_shared<qb::io::tcp::socket>(std::move(*sock)));
        });
        co_return true;
    }

    void
    on(SubscriberSocketReady &ready) {
        transport() = std::move(*ready.socket);
        start();
        _started->store(true, std::memory_order_release);
    }

    void
    on(Protocol::message &&msg) {
        // One frame, many records. `size` is the payload length, so the record count is arithmetic
        // rather than a guess.
        if (_report->wire_complete) {
            _report->subscriber_failed = true;
            qb::io::cerr() << "[subscriber] frame after end-of-stream\n";
            qb::Main::stop();
            return;
        }
        if (msg.size % kWireQuoteSize != 0) {
            fail_subscriber(*_report, "[subscriber] partial wire record");
            return;
        }
        for (std::size_t off = 0; off + kWireQuoteSize <= msg.size; off += kWireQuoteSize) {
            const WireQuote q = decode(msg.data + off);
            if (q.symbol[0] == '\0') { // the end-of-stream sentinel
                if (!is_end_marker(q) || off + kWireQuoteSize != msg.size) {
                    fail_subscriber(*_report, "[subscriber] malformed end-of-stream");
                    return;
                }
                _report->wire_complete = true;
                _report->last_symbol   = Symbol(std::string(_last.symbol, strnlen(_last.symbol, sizeof(_last.symbol))).c_str());
                _report->last_price    = static_cast<double>(_last.last_micros) / 1'000'000.0;
                _report->last_vwap     = static_cast<double>(_last.vwap_micros) / 1'000'000.0;
                _report->last_updates  = _last.updates;
                push<SubscriberDone>(_publisher).records = _records;
                return;
            }
            int        symbol_index = -1;
            const auto name_size    = strnlen(q.symbol, sizeof(q.symbol));
            if (std::any_of(q.symbol + name_size, q.symbol + sizeof(q.symbol), [](char byte) { return byte != '\0'; })) {
                fail_subscriber(*_report, "[subscriber] malformed wire symbol");
                return;
            }
            const std::string_view name(q.symbol, name_size);
            for (int i = 0; i < kSymbols; ++i)
                if (name == kSymbolNames[i]) {
                    symbol_index = i;
                    break;
                }
            if (symbol_index < 0) {
                fail_subscriber(*_report, "[subscriber] unknown wire symbol");
                return;
            }
            auto &observed = _report->wire_symbols[static_cast<std::size_t>(symbol_index)];
            if (q.updates != observed.records + 1 || q.last_seq >= kTicksPerSymbol
                || (observed.seen && (q.last_seq < observed.last_seq || q.volume <= observed.volume))) {
                fail_subscriber(*_report, "[subscriber] non-monotonic symbol record");
                return;
            }
            observed.seen        = true;
            observed.records     = q.updates;
            observed.updates     = q.updates;
            observed.last_seq    = q.last_seq;
            observed.volume      = q.volume;
            observed.last_micros = q.last_micros;
            observed.vwap_micros = q.vwap_micros;
            _last                = q;
            ++_records;
        }
    }

    void
    on(qb::io::async::event::disconnected const &) {
        fail_subscriber(*_report, "[subscriber] disconnected before end-of-stream");
    }
};

} // namespace market_data
