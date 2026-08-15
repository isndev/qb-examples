/**
 * @file examples/06-modules/redis/06-streams.cpp
 * @tier 06-modules
 * @teaches Redis Streams as a work queue and as a log: producers XADD; a consumer group SPLITS the
 *          entries between its competing consumers while a SECOND group gets its own independent
 *          copy; XACK empties the pending list; a plain XREAD needs no group at all; XTRIM bounds
 *          the stream.
 * @demonstrates qb::redis::tcp::client, xadd, xgroup_create, xreadgroup, xack, xpending, xread,
 *               xlen, xtrim, expire, del, spawn, qb::ScopedCoroContext, ctx.sleep,
 *               qb::io::async::task<void>, qb::io::async::task<bool>, registerEvent<E>,
 *               qb::BroadcastId, qb::KillEvent
 * @prerequisites 06-modules/redis/03-coroutines-and-pipelining
 * @expect "[setup] stream key: "
 * @expect "[produce] both producers finished, entries written: "
 * @expect "[workers] the 'workers' group SPLIT its entries between 2 competing consumers: "
 * @expect "[audit] the 'audit' group received its OWN independent copy: "
 * @expect "[ack] every delivered entry was acknowledged, so XPENDING reports "
 * @expect "[xread] a plain XREAD needs no group and carries its own cursor: "
 * @expect "[trim] XTRIM MAXLEN bounded the stream: "
 * @expect "=== streams complete: two groups, competing consumers, XACK, XREAD and XTRIM ==="
 *
 * WHAT THIS FILE REPLACED, AND WHY IT HAD TO BE REWRITTEN RATHER THAN TUNED
 * ------------------------------------------------------------------------
 * The pre-3.0 version of this program was the only failing example in the corpus. It was killed
 * on SIGKILL after 149.9 s in one full run, and measured here at 43.1 s, 73.9 s, 185 s, a 167 s
 * timeout and — from a freshly deleted key, on an idle machine — **300.7 s without finishing**.
 * Three defects, all measured, none of which a smaller `TARGET_READINGS` would have fixed:
 *
 * 1. IT ACKED NOTHING, EVER. Its consumer walked the `xreadgroup` reply with a hard-coded
 *    three-level nesting that matched no shape the server actually sends, so
 *    `process_extracted_data()` was never reached and `xack` was never called. Measured on the
 *    stream it left behind: `entries-read 1021020, pending 1021020` in BOTH groups — every entry
 *    delivered, not one acknowledged — and in a 300 s run the two consumers printed 8 lines
 *    between them, all of them startup banners. The program could therefore never reach its own
 *    completion condition; it only ever ended through a 2 s watchdog that broadcast a shutdown.
 *    That is why this file walks the reply STRUCTURALLY (`collect_entries` below) instead of
 *    assuming a nesting: the shape differs between RESP2 and RESP3, and the entry id is the one
 *    invariant both share.
 *
 * 2. IT SPAWNED A COROUTINE PER LOOP TURN. Both actor kinds did their Redis work from
 *    `on(qb::LoopEvent const&)`, which fires on every turn of the core, and each turn spawned a
 *    fresh coroutine holding one more in-flight command on the SAME connection. Nothing bounded
 *    the concurrency, and the guard that was supposed to stop production read a counter that is
 *    only incremented AFTER the await — so the stream overshot its own target (measured
 *    XLEN 1,000,104 against TARGET_READINGS 1,000,000). Here each actor runs exactly ONE
 *    long-lived coroutine with one command in flight at a time.
 *
 * 3. ITS RUNTIME DEPENDED ON THE PREVIOUS RUN. The coordinator deleted a FIXED stream key at
 *    startup, which is correct as far as it goes — but deleting a key holding a million entries
 *    and ~230 MB is O(N) server-side work, charged to the next run's startup while its producers
 *    are already writing. That, and not any logical accumulation, is why the same program timed
 *    itself differently on every attempt. This version uses a key unique to the run, so a second
 *    run measures exactly what the first did.
 *
 * WHAT IT COSTS NOW: 40 entries, and the whole program is a fraction of a second.
 *
 * THE TWO STREAM SEMANTICS, WHICH ARE EASY TO CONFUSE
 * ---------------------------------------------------
 * Both are demonstrated below, side by side, because a stream does both at once:
 *
 *   * WITHIN one consumer group, an entry goes to exactly ONE consumer. Two consumers in the
 *     `workers` group therefore SPLIT the 40 entries — that is the work-queue semantic, and the
 *     split is why adding a consumer adds throughput.
 *   * ACROSS groups, every group gets EVERY entry. The `audit` group reads all 40 on its own
 *     cursor, entirely unaffected by what `workers` did — that is the fan-out semantic.
 *
 * A group created at id "0" is delivered the stream from the beginning, so `xgroup_create` here
 * is ORDER-INDEPENDENT with respect to the producers: it does not matter whether a consumer
 * joins before or after the entries are written. That is what removes the startup race the
 * previous version had between its `del` and its consumers' `xgroup_create`.
 */

#include <qbm/redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/string.h>
#include <qb/json.h>
#include <qb/system/parse.h>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#define REDIS_URI "tcp://localhost:6379"

static constexpr int ENTRIES_PER_PRODUCER = 20;
static constexpr int PRODUCER_COUNT       = 2;
static constexpr int TOTAL_ENTRIES        = ENTRIES_PER_PRODUCER * PRODUCER_COUNT;
static constexpr int CONSUMER_COUNT       = 3; // two in `workers`, one in `audit`
static constexpr int TRIM_MAXLEN          = 10;

static constexpr char GROUP_WORKERS[] = "workers";
static constexpr char GROUP_AUDIT[]   = "audit";

/**
 * @brief The stream key, unique to this run.
 *
 * Namespaced like every other key in this tier, and suffixed with the process start time so that
 * a second run neither reads nor pays for the first one's entries. The previous version shared
 * one fixed key across every run and opened by deleting whatever it found — see defect 3 above.
 * A run that is killed leaves at most TOTAL_ENTRIES behind, and the producers put a TTL on the
 * key after their first write so even that expires on its own.
 */
static const std::string STREAM = "qb:example:streams:readings:" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

/**
 * @brief Set by the coordinator before it stops the engine, read by main() after join().
 *
 * Safe by ordering rather than by luck: `qb::Main::join()` happens-after every actor has been
 * destroyed, so the read cannot race the write. It is an atomic so that the ordering is stated
 * in the type rather than in a comment somebody can delete.
 */
static std::atomic<bool> RUN_OK{false};

// ---------------------------------------------------------------------------------------------
// Reading the reply.
//
// `xread` and `xreadgroup` are declared to return `qb::json` — "loosely structured server data"
// (qbm/redis/readme/stream_commands.md:56) — and the nesting genuinely differs between RESP2 and
// RESP3. Hard-coding one shape is what made the previous version ack nothing, so this walks the
// tree instead and collects every object member whose KEY parses as a stream id ("<ms>-<seq>").
// That is the one thing both protocol shapes agree on.
// ---------------------------------------------------------------------------------------------
struct StreamEntry {
    std::string id;
    qb::json    fields;
};

static bool
is_stream_id(std::string_view s) {
    const auto dash = s.find('-');
    if (dash == std::string_view::npos || dash == 0 || dash + 1 == s.size())
        return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i != dash && (s[i] < '0' || s[i] > '9'))
            return false;
    return true;
}

static void
collect_entries(const qb::json &node, std::vector<StreamEntry> &out) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (is_stream_id(it.key()))
                out.push_back(StreamEntry{it.key(), it.value()});
            else
                collect_entries(it.value(), out);
        }
    } else if (node.is_array()) {
        for (const auto &child : node)
            collect_entries(child, out);
    }
}

/// One field of an entry. RESP3 gives an object; RESP2 flattens the fields to [k, v, k, v, ...].
static std::string
field_of(const qb::json &fields, const std::string &name) {
    if (fields.is_object()) {
        const auto it = fields.find(name);
        return (it != fields.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    }
    if (fields.is_array())
        for (std::size_t i = 0; i + 1 < fields.size(); i += 2)
            if (fields[i].is_string() && fields[i].get<std::string>() == name)
                return fields[i + 1].is_string() ? fields[i + 1].get<std::string>() : std::string{};
    return {};
}

// ---------------------------------------------------------------------------------------------
// Events. Every payload is bounded (`qb::string<N>`) or a POD: the engine relocates an event with
// memcpy and never runs the source destructor, so no member may hold a pointer into itself.
// ---------------------------------------------------------------------------------------------
struct ProducerDoneEvent : qb::Event {
    qb::string<32> sensor_id;
    int            written;

    ProducerDoneEvent(const std::string &id, int n)
        : sensor_id(id)
        , written(n) {}
};

struct ConsumerDoneEvent : qb::Event {
    qb::string<32> group;
    qb::string<32> consumer;
    int            acked;

    ConsumerDoneEvent(const std::string &g, const std::string &c, int n)
        : group(g)
        , consumer(c)
        , acked(n) {}
};

/// Broadcast once, after BOTH producers have reported: "no further entry will ever be written".
/// It is what lets a consumer tell "the stream is empty for now" from "the stream is finished".
struct ProductionDoneEvent : qb::Event {};

// ---------------------------------------------------------------------------------------------
// Producer — one actor, one coroutine, one XADD in flight at a time.
// ---------------------------------------------------------------------------------------------
class SensorProducerActor : public qb::Actor {
private:
    qb::string<32>         _sensor_id;
    int                    _target;
    int                    _written = 0;
    qb::ActorId            _coordinator;
    qb::redis::tcp::client _redis;
    std::mt19937           _rng;

public:
    SensorProducerActor(std::string sensor_id, int target, qb::ActorId coordinator)
        : _sensor_id(sensor_id)
        , _target(target)
        , _coordinator(coordinator)
        , _redis(qb::io::uri(REDIS_URI))
        , _rng(std::random_device{}()) {}

    ~SensorProducerActor() noexcept override = default;

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<qb::KillEvent>(*this);

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "SensorProducer [" << _sensor_id << "] failed to connect to Redis" << std::endl;
            co_return false;
        }
        qb::io::cout() << "SensorProducer [" << _sensor_id << "] connected to Redis" << std::endl;

        // ONE coroutine for the whole production run. The previous version spawned one per turn
        // of the event loop and let them pile up on a single connection — see defect 2 above.
        // `_redis` is a MEMBER, so the client and its pending replies die with the actor; the
        // coroutine is safe by ownership, not by `spawn`'s cancellation scope, which a qbm
        // command awaiter never registers with.
        spawn([this](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            std::uniform_real_distribution<> temp(15.0, 40.0);
            std::uniform_real_distribution<> humid(30.0, 90.0);
            std::uniform_real_distribution<> press(980.0, 1030.0);

            while (_written < _target && is_alive()) {
                const std::vector<std::pair<std::string, std::string>> fields = {
                    {"sensor_id", _sensor_id.c_str()},
                    {"temperature", std::to_string(temp(_rng))},
                    {"humidity", std::to_string(humid(_rng))},
                    {"pressure", std::to_string(press(_rng))}
                };

                auto added = co_await _redis.xadd(STREAM, fields);
                if (!added.ok()) {
                    qb::io::cerr() << "SensorProducer [" << _sensor_id << "] XADD failed: " << added.error() << std::endl;
                    break;
                }
                if (++_written == 1) {
                    // The key exists from here on, so a TTL now covers every way this run can be
                    // interrupted. The coordinator deletes the key outright on the normal path.
                    (void) co_await _redis.expire(STREAM, 300);
                }

                // Pace the writers so both consumers of the `workers` group get turns; without
                // it one consumer can win every read and the split is invisible. It is also the
                // back-pressure this program has: one command in flight, then a yield.
                co_await ctx.sleep(std::chrono::milliseconds(2));
            }

            qb::io::cout() << "SensorProducer [" << _sensor_id << "] wrote " << _written << " entries" << std::endl;
            push<ProducerDoneEvent>(_coordinator, _sensor_id.c_str(), _written);
            kill();
        });

        co_return true;
    }

    void
    on(const qb::KillEvent &) {
        kill();
    }
};

// ---------------------------------------------------------------------------------------------
// Consumer — joins a group, drains it, acknowledges everything, reports what IT acked.
// ---------------------------------------------------------------------------------------------
class StreamConsumerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis;
    qb::string<32>         _group;
    qb::string<32>         _consumer;
    qb::ActorId            _coordinator;
    int                    _acked           = 0;
    int                    _alerts          = 0;
    double                 _alert_threshold = 35.0;
    bool                   _production_done = false;

public:
    StreamConsumerActor(std::string group, std::string consumer, qb::ActorId coordinator)
        : _redis(qb::io::uri(REDIS_URI))
        , _group(group)
        , _consumer(consumer)
        , _coordinator(coordinator) {}

    ~StreamConsumerActor() noexcept override = default;

    qb::io::async::task<bool>
    onInit() override {
        // Registered BEFORE the first co_await, so no event can arrive at an actor that is not
        // yet listening for it.
        registerEvent<ProductionDoneEvent>(*this);
        registerEvent<qb::KillEvent>(*this);

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "StreamConsumer [" << _consumer << "] failed to connect to Redis" << std::endl;
            co_return false;
        }

        // id "0" + mkstream: give this group the stream FROM THE BEGINNING, creating the key if
        // it is not there yet. That is what makes joining order-independent — a group created
        // after the producers have finished still receives every entry. BUSYGROUP (another
        // consumer of the same group got here first) is the expected answer for the second one.
        auto created = co_await _redis.xgroup_create(STREAM, _group.c_str(), "0", true);
        qb::io::cout() << "StreamConsumer [" << _consumer << "] " << (created.ok() ? "created" : "joined existing") << " group [" << _group
                       << "]" << std::endl;

        spawn([this](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // A wall-clock bound so that a server which stops answering ends this program with a
            // reported shortfall instead of the hang the previous version could produce.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

            while (is_alive() && std::chrono::steady_clock::now() < deadline) {
                // ">" = entries never delivered to ANY consumer of this group. Inside a group
                // that makes the read destructive-by-delivery, which is exactly what splits the
                // work between competing consumers.
                auto reply = co_await _redis.xreadgroup(STREAM, _group.c_str(), _consumer.c_str(), ">", 8, 50);

                std::vector<StreamEntry> entries;
                if (reply.ok() && !reply.result().is_null())
                    collect_entries(reply.result(), entries);

                for (const auto &entry : entries) {
                    const std::string sensor = field_of(entry.fields, "sensor_id");
                    const std::string temp   = field_of(entry.fields, "temperature");
                    if (!temp.empty() && qb::to_number<double>(temp).value_or(0.0) > _alert_threshold)
                        ++_alerts;

                    // The acknowledgement the previous version never reached. Until an entry is
                    // acked it stays in this group's pending list (its PEL) and is redeliverable
                    // to another consumer — which is how a stream survives a consumer dying
                    // mid-entry, and why "read" and "done" have to be two separate steps.
                    auto acked = co_await _redis.xack(STREAM, _group.c_str(), entry.id);
                    if (acked.ok())
                        _acked += static_cast<int>(acked.result());
                    if (sensor.empty())
                        qb::io::cerr() << "StreamConsumer [" << _consumer << "] entry " << entry.id << " had no sensor_id" << std::endl;
                }

                if (entries.empty()) {
                    // Empty means "nothing undelivered right now". Only ProductionDoneEvent can
                    // turn that into "and there never will be".
                    if (_production_done)
                        break;
                    co_await ctx.sleep(std::chrono::milliseconds(5));
                }
            }

            qb::io::cout() << "StreamConsumer [" << _consumer << "] acked " << _acked << " entries (" << _alerts << " over threshold)"
                           << std::endl;
            push<ConsumerDoneEvent>(_coordinator, _group.c_str(), _consumer.c_str(), _acked);
            kill();
        });

        co_return true;
    }

    void
    on(const ProductionDoneEvent &) {
        _production_done = true;
    }

    void
    on(const qb::KillEvent &) {
        kill();
    }
};

// ---------------------------------------------------------------------------------------------
// Coordinator — owns the run: counts the reports, then measures the stream and cleans it up.
// ---------------------------------------------------------------------------------------------
class CoordinatorActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis;
    int                    _producers_done = 0;
    int                    _consumers_done = 0;
    int                    _written        = 0;
    int                    _workers_acked  = 0;
    int                    _audit_acked    = 0;

public:
    CoordinatorActor()
        : _redis(qb::io::uri(REDIS_URI)) {}

    ~CoordinatorActor() noexcept override = default;

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<ProducerDoneEvent>(*this);
        registerEvent<ConsumerDoneEvent>(*this);
        registerEvent<qb::KillEvent>(*this);

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "Coordinator failed to connect to Redis" << std::endl;
            co_return false;
        }
        qb::io::cout() << "Coordinator connected to Redis" << std::endl;
        qb::io::cout() << "[setup] stream key: " << STREAM << " (unique to this run, so a second run costs what the first did)" << std::endl;
        co_return true;
    }

    void
    on(const ProducerDoneEvent &event) {
        _written += event.written;
        if (++_producers_done < PRODUCER_COUNT)
            return;

        qb::io::cout() << "[produce] both producers finished, entries written: " << _written << std::endl;

        // Cores 2 and 3 hold nothing but consumers, and an actor that never registered for this
        // event simply ignores it. Broadcasting is what lets the coordinator say "production is
        // over" without keeping a roster of consumer ids it would have to be told about first.
        for (int core = 2; core <= 3; ++core)
            push<ProductionDoneEvent>(qb::BroadcastId(core));
    }

    void
    on(const ConsumerDoneEvent &event) {
        if (event.group == GROUP_WORKERS)
            _workers_acked += event.acked;
        else
            _audit_acked += event.acked;

        if (++_consumers_done < CONSUMER_COUNT)
            return;

        // Spelled as one literal each, not assembled with <<, so that the @expect lines in the
        // header block are strings this file demonstrably contains.
        qb::io::cout() << "[workers] the 'workers' group SPLIT its entries between 2 competing consumers: " << _workers_acked << " of "
                       << _written << " acked in total" << std::endl;
        qb::io::cout() << "[audit] the 'audit' group received its OWN independent copy: " << _audit_acked << " of " << _written << std::endl;

        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> { co_await finish(); });
    }

    /// Everything that can only be measured once every consumer has stopped reading.
    qb::io::async::task<void>
    finish() {
        auto       pending_workers = co_await _redis.xpending(STREAM, GROUP_WORKERS);
        auto       pending_audit   = co_await _redis.xpending(STREAM, GROUP_AUDIT);
        const auto still_pending   = pending_count(pending_workers) + pending_count(pending_audit);
        qb::io::cout() << "[ack] every delivered entry was acknowledged, so XPENDING reports " << still_pending
                       << " entries still pending in the two groups" << std::endl;

        // XREAD is the other half of the API: no group, no delivery state on the server, no
        // acknowledgement. The cursor is the caller's — pass the last id you saw to get the next
        // batch. It reads the same entries the groups already consumed, because a group's
        // bookkeeping is the group's, not the stream's.
        auto                     tail = co_await _redis.xread(STREAM, "0", 5);
        std::vector<StreamEntry> tailed;
        if (tail.ok() && !tail.result().is_null())
            collect_entries(tail.result(), tailed);
        qb::io::cout() << "[xread] a plain XREAD needs no group and carries its own cursor: " << tailed.size()
                       << " entries read back from id 0, first id " << (tailed.empty() ? std::string("-") : tailed.front().id) << std::endl;

        auto len_before = co_await _redis.xlen(STREAM);
        (void) co_await _redis.xtrim(STREAM, TRIM_MAXLEN);
        auto len_after = co_await _redis.xlen(STREAM);
        qb::io::cout() << "[trim] XTRIM MAXLEN bounded the stream: " << len_before.result() << " -> " << len_after.result()
                       << " entries (a stream grows forever until something trims it)" << std::endl;

        (void) co_await _redis.del(STREAM);

        const bool ok = _written == TOTAL_ENTRIES && _workers_acked == TOTAL_ENTRIES && _audit_acked == TOTAL_ENTRIES && still_pending == 0
                        && len_after.result() == TRIM_MAXLEN;
        if (!ok)
            qb::io::cerr() << "FAILED: expected " << TOTAL_ENTRIES << " written and acked by each group, 0 pending, " << TRIM_MAXLEN
                           << " left after the trim" << std::endl;
        RUN_OK.store(ok);

        qb::io::cout() << "\n=== streams complete: two groups, competing consumers, XACK, XREAD and XTRIM ===" << std::endl;
        qb::Main::stop();
    }

    void
    on(const qb::KillEvent &) {
        kill();
    }

private:
    /// The extended XPENDING form answers with one element per pending entry, so the count is the
    /// array's size. A healthy run acked everything and gets an empty reply.
    static std::size_t
    pending_count(const qb::redis::Reply<qb::json> &reply) {
        if (!reply.ok() || reply.result().is_null())
            return 0;
        return reply.result().is_array() ? reply.result().size() : 0;
    }
};

int
main() {
    qb::io::async::init();
    qb::io::cout() << "Starting Redis Stream Processor Example" << std::endl;

    qb::Main engine;

    const auto coordinator = engine.addActor<CoordinatorActor>(0);

    engine.addActor<SensorProducerActor>(1, "sensor001", ENTRIES_PER_PRODUCER, coordinator);
    engine.addActor<SensorProducerActor>(1, "sensor002", ENTRIES_PER_PRODUCER, coordinator);

    // Two consumers of ONE group on core 2 — they compete, and between them they see each entry
    // exactly once. One consumer of a SECOND group on core 3 — it sees all of them.
    engine.addActor<StreamConsumerActor>(2, GROUP_WORKERS, "worker-a", coordinator);
    engine.addActor<StreamConsumerActor>(2, GROUP_WORKERS, "worker-b", coordinator);
    engine.addActor<StreamConsumerActor>(3, GROUP_AUDIT, "auditor", coordinator);

    engine.start(true);
    engine.join();

    qb::io::cout() << "Redis Stream Processor Example completed" << std::endl;
    return RUN_OK.load() ? 0 : 1;
}
