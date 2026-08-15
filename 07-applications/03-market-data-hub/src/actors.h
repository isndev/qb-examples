/*
 * market-data-hub — the four actors, and the one decision each of them makes.
 *
 *   IngestActor      drains a lock-free ring written by a FOREIGN thread and routes each tick to
 *                    a shard with `WorkerPool::for_key`. Hot path: `send<>`, not `push<>`.
 *   AggregatorActor  keeps per-symbol state and COALESCES its output with `qb::batcher`.
 *   PublisherActor   owns the TCP server, writes batches as binary frames, and measures the
 *                    end-to-end latency of every quote.
 *   SubscriberActor  the wire's other end, in this same process, so the format is proven rather
 *                    than asserted.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
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
    TickRing                *_ring;
    std::atomic<bool>       *_feeding;
    std::vector<qb::ActorId> _shard_ids;
    qb::WorkerPool           _shards;
    Report                  *_report = nullptr;
    std::uint64_t            _routed = 0;
    bool                     _ended  = false;

public:
    IngestActor(TickRing *ring, std::atomic<bool> *feeding, std::vector<qb::ActorId> shards, Report *report)
        : _ring(ring)
        , _feeding(feeding)
        , _shard_ids(std::move(shards))
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
            // only for a trivially-destructible event; it skips the destructor bookkeeping
            // `push<>` does. Ticks are independent samples, so ordering between them buys
            // nothing — and this is exactly the case `send` exists for.
            // Note the shape: `send` CONSTRUCTS the event from these arguments and returns void,
            // where `push` returns a reference you fill in afterwards. There is no reference to
            // hold here, which is also why there is nothing to invalidate.
            send<TickEvent>(dest, Symbol(kSymbolNames[t.sym_ix]), t.at_ns, t.seq, t.price, t.volume);
            ++_routed;
        }

        if (!_ended && !_feeding->load(std::memory_order_acquire) && _ring->empty()) {
            _ended          = true;
            _report->routed = _routed;
            _report->shards = static_cast<std::uint32_t>(_shards.size());
            for (auto id : _shard_ids)
                push<EndOfFeed>(id); // ORDERED: it must arrive after every send<> above
            unregisterCallback(*this);
        }
    }
};

// ---------------------------------------------------------------------------------------
// AGGREGATOR — per-symbol state, and one publish per batch instead of one per tick.
// ---------------------------------------------------------------------------------------
class AggregatorActor : public qb::Actor {
    struct Book {
        double        vwap_num = 0;
        std::uint64_t vwap_den = 0;
        double        last     = 0;
        std::uint32_t volume   = 0;
        std::uint32_t updates  = 0;
    };

    qb::ActorId                          _publisher;
    qb::unordered_map<std::string, Book> _books;
    std::uint32_t                        _emitted = 0;

    // Held as a MEMBER, and `[this]` is safe here precisely because it is: the window timer is
    // bound to this actor's scope, so a flush can never fire after the actor is gone.
    qb::batcher<Quote> _batch{kBatchMax, qb::duration{std::chrono::milliseconds(4)}, [this](std::vector<Quote> &&quotes) {
                                  publish(std::move(quotes));
                              }};

public:
    explicit AggregatorActor(qb::ActorId publisher)
        : _publisher(publisher) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<TickEvent>(*this);
        registerEvent<EndOfFeed>(*this);
        co_return true;
    }

    void
    on(TickEvent &t) {
        Book &b = _books[std::string(t.symbol.c_str())];
        b.vwap_num += t.price * t.volume;
        b.vwap_den += t.volume;
        b.last = t.price;
        b.volume += t.volume;
        ++b.updates;

        Quote q;
        q.symbol  = t.symbol;
        q.at_ns   = t.at_ns; // carried through, so the publisher measures FEED to WIRE
        q.last    = b.last;
        q.vwap    = b.vwap_den ? b.vwap_num / static_cast<double>(b.vwap_den) : b.last;
        q.volume  = b.volume;
        q.updates = b.updates;

        // May flush RIGHT HERE if this quote fills the buffer.
        _batch.add(context(), std::move(q));
    }

    void
    on(EndOfFeed const &) {
        // The batcher's window timer is cancelled with the actor, so buffered items are DROPPED
        // rather than flushed at shutdown. A final manual flush is not optional.
        _batch.flush();
        push<AggregatorDone>(_publisher).emitted = _emitted;
    }

private:
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

    /// A subscriber has attached. The feed thread is waiting for exactly this: publishing into an
    /// empty session list would send those quotes nowhere and make the record count a lie.
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
class SubscriberActor
    : public qb::Actor
    , public qb::io::use<SubscriberActor>::tcp::client<> {
    qb::ActorId   _publisher;
    std::uint16_t _port;
    Report       *_report  = nullptr;
    std::uint32_t _records = 0;
    WireQuote     _last{};

public:
    using Protocol = QuoteFeed<SubscriberActor>;

    SubscriberActor(qb::ActorId publisher, std::uint16_t port, Report *report)
        : _publisher(publisher)
        , _port(port)
        , _report(report) {}

    qb::io::async::task<bool>
    onInit() override {
        // THE CONNECT IS SPAWNED, NOT AWAITED IN `onInit`, AND THAT IS A MEASUREMENT.
        //
        // The obvious form — `co_await qb::io::async::tcp::connect<transport::tcp>(uri, 2s)`
        // written directly in `onInit` — SUSPENDS AND NEVER RESUMES on a VirtualCore. Measured on
        // this tree: the TCP connection really is established (the publisher's `on(IOSession&)`
        // fires, `sessions().size() == 1`), but the awaiting `onInit` is never woken, so this
        // actor stays Activating for ever and the program hangs with no diagnostic — the 5 s
        // activation deadline did not visibly reap it either. The SAME expression inside a
        // `spawn()` from the same actor resumes normally, which is what this does.
        //
        // The cost of the workaround is stated rather than hidden: `onInit` returns true
        // immediately, so this actor is ACTIVE before its socket exists. Anything that must not
        // be sent before the connection is up has to wait for evidence — here, nothing does,
        // because the subscriber only ever reads.
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto sock = co_await qb::io::async::tcp::connect<qb::io::transport::tcp>(qb::io::uri("tcp://127.0.0.1:" + std::to_string(_port)),
                                                                                     std::chrono::seconds(2));
            if (!sock.has_value()) {
                qb::io::cerr() << "[subscriber] could not connect to the feed\n";
                co_return;
            }
            transport() = std::move(*sock);
            start();
        });
        co_return true;
    }

    void
    on(Protocol::message &&msg) {
        // One frame, many records. `size` is the payload length, so the record count is arithmetic
        // rather than a guess.
        for (std::size_t off = 0; off + kWireQuoteSize <= msg.size; off += kWireQuoteSize) {
            const WireQuote q = decode(msg.data + off);
            if (q.symbol[0] == '\0') { // the end-of-stream sentinel
                _report->last_symbol  = Symbol(std::string(_last.symbol, strnlen(_last.symbol, sizeof(_last.symbol))).c_str());
                _report->last_price   = static_cast<double>(_last.last_micros) / 1'000'000.0;
                _report->last_vwap    = static_cast<double>(_last.vwap_micros) / 1'000'000.0;
                _report->last_updates = _last.updates;
                push<SubscriberDone>(_publisher).records = _records;
                return;
            }
            _last = q;
            ++_records;
        }
    }

    void
    on(qb::io::async::event::disconnected const &) {}
};

} // namespace market_data
