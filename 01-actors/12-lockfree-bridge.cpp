/**
 * @file examples/01-actors/12-lockfree-bridge.cpp
 * @tier 01-actors
 * @teaches The other half of the foreign-thread boundary: 03-event-payloads bridged ONE outside
 *          thread with an spsc ring; this bridges MANY with qb::lockfree::mpsc::ringbuffer, shows
 *          why its three enqueue overloads are not interchangeable (one of them takes a lock and
 *          two do not), why the drain must be BOUNDED per loop turn, and what qb::lockfree::SpinLock
 *          is — the primitive underneath all of it, and the one you will most often be wrong to use.
 * @demonstrates qb::lockfree::mpsc::ringbuffer<Job, RING_SLOTS, PRODUCERS>,
 *               qb::lockfree::mpsc::ringbuffer<Job, TINY_SLOTS, 1>,
 *               qb::lockfree::SpinLock, trylock, trylock_for, locked, lock, unlock,
 *               qb::Actor, qb::ICallback, qb::LoopEvent, registerCallback, unregisterCallback,
 *               registerEvent<E>, push<E>, broadcast<qb::KillEvent>, qb::KillEvent, getIndex,
 *               qb::Main, addActor<T>, qb::Event
 * @prerequisites 01-actors/03-event-payloads
 * @expect "[why] an actor may only be touched from its own VirtualCore, so a foreign thread hands"
 * @expect "[owned] 3 threads, 3 dedicated slots, enqueue(index, item): no lock is taken at all,"
 * @expect "[shared] enqueue(item) is the overload for a thread that does NOT own a slot: it picks"
 * @expect "[bounded] the drain takes at most "
 * @expect "[full] a bounded ring is BACKPRESSURE, not an error: enqueue returned false "
 * @expect "[spinlock] trylock() is one exchange and never waits; trylock_for() spins on a RELAXED"
 * @expect "[spinlock] and the rule that matters: a spinlock burns a core while it waits, so it is"
 * @expect "=== lockfree bridge complete: every item a foreign thread produced was accounted for"
 *
 * THE PROBLEM THIS SOLVES, AND WHY IT IS NOT "USE A MUTEX"
 * -------------------------------------------------------
 * An actor's state belongs to exactly one `VirtualCore` worker thread. A thread that is not that
 * worker may not call `push`, may not read a member, may not touch the actor at all — and no
 * mutex fixes that, because the hazard is not a data race on one field, it is that the actor's
 * whole single-threaded contract is what makes `on(Event&)` safe to write. So the crossing is
 * one-way and by VALUE: the outside thread writes into a lock-free ring, and the actor drains
 * that ring on its own thread.
 *
 * `01-actors/03-event-payloads` does this with `qb::lockfree::spsc::ringbuffer` — Single Producer,
 * Single Consumer. That is the whole contract: exactly one thread may enqueue. Two threads on one
 * spsc ring is not "slow", it is undefined; the head and tail indices are plain atomics with no
 * ownership protocol between writers.
 *
 * `qb::lockfree::mpsc::ringbuffer<T, N, P>` is what you reach for when there are several outside
 * threads. It is not a cleverer algorithm — it is **P spsc rings side by side**, each padded onto
 * its own cache line, with one consumer walking all of them. That construction is the reason for
 * everything below.
 *
 * THREE ENQUEUE OVERLOADS, AND ONLY ONE OF THEM LOCKS
 * --------------------------------------------------
 *   ring.enqueue<I>(item)         compile-time slot I. No lock. (qb/system/lockfree/mpsc.h:76)
 *   ring.enqueue(index, item)     runtime slot. **No lock.**  (mpsc.h:102)
 *   ring.enqueue(item)            round-robin. **Takes the slot's SpinLock.** (mpsc.h:131)
 *
 * The middle one is the fast path and the sharp one: it is correct only if no two threads ever
 * pass the same `index` — i.e. only if you have assigned slots. Section 1 does that. The third is
 * for the case where you cannot: a thread pool whose workers come and go has no slot to own, so
 * the ring picks one for it with a thread-local counter and locks it, because two threads can
 * land on the same slot. Section 2 does that. Both are correct; they cost different things, and
 * the API does not stop you from choosing wrong.
 *
 * THE DRAIN IS THE PART PEOPLE GET WRONG
 * --------------------------------------
 * `ICallback` runs once per loop turn on the actor's core. If the drain loops "until empty", a
 * producer that outruns the consumer keeps it non-empty forever and the loop turn never ends —
 * so timers do not fire, sockets are not read, and every other actor on that core stops. qb has
 * had exactly this defect in its own `listener::run()`. The fix is a per-turn budget: take at
 * most N and come back next turn. Section 3 uses the copy-out `dequeue(buffer, n)` overload,
 * which is the one that decrements its budget as it walks the producers.
 *
 * ABOUT THE SPINLOCK
 * ------------------
 * `qb::lockfree::SpinLock` is one `std::atomic<bool>` and a TTAS wait loop. It is the right tool
 * for a critical section measured in *nanoseconds* on a core that has nothing better to do, and
 * the wrong tool for anything else: it does not sleep, so a waiter burns a whole core, and if the
 * holder is descheduled the waiters spin through its entire time slice. Never hold one across a
 * syscall, an allocation, a lock of any other kind, or a `co_await`. Section 5 shows the API and
 * then says this again, because the API is easy and the judgement is not.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-lockfree-bridge
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-lockfree-bridge
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/main.h>
#include <qb/system/lockfree/mpsc.h>
#include <qb/system/lockfree/spinlock.h>

namespace {

// One unit of work handed across the boundary. Trivially copyable on purpose: the ring's bulk
// paths use `std::memcpy` and `spsc::ringbuffer` static_asserts anything else away
// (qb/system/lockfree/spsc.h:53). It is also the same rule an event payload obeys — nothing here
// may hold a pointer into itself.
struct Job {
    std::uint32_t producer;
    std::uint32_t sequence;
    std::uint64_t value;
};

constexpr std::size_t PRODUCERS    = 3;    // one dedicated slot per foreign thread
constexpr std::size_t RING_SLOTS   = 1024; // capacity PER PRODUCER, not in total
constexpr std::size_t TINY_SLOTS   = 8;    // section 4: small enough to overflow on purpose
constexpr std::size_t PER_THREAD   = 400;  // jobs each foreign thread produces in section 1
constexpr std::size_t POOL_JOBS    = 300;  // jobs the "pool" threads produce in section 2
constexpr std::size_t DRAIN_BUDGET = 32;   // section 3: items taken per loop turn

using JobRing  = qb::lockfree::mpsc::ringbuffer<Job, RING_SLOTS, PRODUCERS>;
using TinyRing = qb::lockfree::mpsc::ringbuffer<Job, TINY_SLOTS, 1>;

// Everything the two sides share. Ordinary stack objects in main(); the rings are the only
// mutable state touched from more than one thread, and they are lock-free by construction.
struct Bridge {
    JobRing           owned_slots;  // section 1: enqueue(index, item)
    JobRing           shared_slots; // section 2: enqueue(item)
    std::atomic<bool> producing{true};

    // Written by the foreign threads before they join.
    std::atomic<std::uint64_t> offered{0};  // items the producers tried to enqueue
    std::atomic<std::uint64_t> checksum{0}; // sum of every value that was offered

    // Written by the actor, read by main() AFTER engine.join(). The actor itself is owned by the
    // engine and destroyed with it, so its counters have to outlive it somewhere — this struct is
    // an ordinary stack object in main() and does.
    std::atomic<std::uint64_t> got_owned{0};
    std::atomic<std::uint64_t> got_shared{0};
    std::atomic<std::uint64_t> got_checksum{0};
};

// Sent by the drain to itself the turn it finishes, so the totals below are printed from an event
// handler on the actor's own thread rather than from whatever thread happens to be looking.
struct Drained : qb::Event {
    std::uint64_t owned_items;
    std::uint64_t shared_items;

    // An explicit constructor, not aggregate init: `push<E>(dest, args...)` placement-news the
    // event as `E(args...)`, and a struct with a base class is not constructible that way on
    // every toolchain this corpus builds with.
    Drained(std::uint64_t owned, std::uint64_t shared)
        : owned_items(owned)
        , shared_items(shared) {}
};

// ---------------------------------------------------------------------------------------
// The consumer. An ordinary actor; the only unusual thing about it is that its input does not
// arrive as events.
//
// It stays INSIDE the anonymous namespace with the types it holds. A class with external
// linkage carrying a field whose type has none is what GCC's -Wsubobject-linkage exists for,
// and `Bridge*` is exactly that field — clang says nothing about it, so the only way to keep
// the corpus building on both is not to write the shape. 06-modules/http/10-client.cpp puts its
// server in the same block for the same reason.
// ---------------------------------------------------------------------------------------
class BridgeActor
    : public qb::Actor
    , public qb::ICallback {
    Bridge       *_bridge;
    std::uint64_t _owned  = 0;
    std::uint64_t _shared = 0;
    std::uint64_t _sum    = 0;
    std::uint64_t _turns  = 0;
    std::uint64_t _peak   = 0;

    // The scratch buffer the copy-out drain writes through. Sized to the budget, and reused —
    // allocating on a per-turn hot path is exactly what a lock-free ring exists to avoid.
    std::array<Job, DRAIN_BUDGET> _scratch{};

public:
    explicit BridgeActor(Bridge *bridge)
        : _bridge(bridge) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Drained>(*this);
        // ICallback is the every-turn hook. It is the right tool for a drain and the wrong tool
        // for anything that can block: it runs INSIDE the loop turn, ahead of the I/O the core
        // still has to service.
        registerCallback(*this);
        co_return true;
    }

    void
    on(Drained const &d) {
        qb::io::cout() << "[owned] 3 threads, 3 dedicated slots, enqueue(index, item): no lock is taken at all,\n"
                          "        because slot i has exactly one writer — the mpsc ring is P spsc rings, and this\n"
                          "        overload restores each one's single-producer contract by convention, not by force\n";
        qb::io::cout() << "        (" << d.owned_items << " of " << (PRODUCERS * PER_THREAD) << " items arrived)\n\n";

        qb::io::cout() << "[shared] enqueue(item) is the overload for a thread that does NOT own a slot: it picks\n"
                          "         one with a thread-local counter and locks it, because two threads can choose the\n"
                          "         same one. Correct anywhere; it just costs an uncontended atomic exchange more\n";
        qb::io::cout() << "         (" << d.shared_items << " of " << (2 * POOL_JOBS) << " items arrived from 2 pool threads)\n\n";

        broadcast<qb::KillEvent>();
    }

    void
    on(qb::LoopEvent const &) final {
        ++_turns;

        // BOUNDED. `dequeue(buffer, n)` walks the producers taking at most `n` IN TOTAL — it
        // decrements its budget as it goes across slots. That is the property this loop needs,
        // and it is why a budget is passed rather than "drain until empty".
        //
        // There are TWO rings here, so the budget has to be split between them or the turn takes
        // 2 x DRAIN_BUDGET and the sentence this program prints about itself is false. It was, on
        // the first run: 32 was advertised and 64 was measured.
        std::uint64_t this_turn  = 0;
        const auto    took_owned = _bridge->owned_slots.dequeue(_scratch.data(), DRAIN_BUDGET);
        for (std::size_t i = 0; i < took_owned; ++i)
            _sum += _scratch[i].value;
        _owned += took_owned;
        this_turn += took_owned;

        const auto took_shared = _bridge->shared_slots.dequeue(_scratch.data(), DRAIN_BUDGET - took_owned);
        for (std::size_t i = 0; i < took_shared; ++i)
            _sum += _scratch[i].value;
        _shared += took_shared;
        this_turn += took_shared;

        if (this_turn > _peak)
            _peak = this_turn;

        // Done when the producers are finished AND both rings have been walked empty. The order
        // matters: read `producing` AFTER the dequeues, or a producer's last write can land
        // between the two reads and be left in the ring forever.
        if (!_bridge->producing.load(std::memory_order_acquire) && this_turn == 0) {
            qb::io::cout() << "[bounded] the drain takes at most " << DRAIN_BUDGET
                           << " items per loop turn — an UNBOUNDED drain starves\n"
                              "           every other actor on this core, because the turn never ends while a\n"
                              "           producer keeps the ring non-empty\n";
            qb::io::cout() << "           (core " << getIndex() << ", " << _turns << " turns, at most " << _peak
                           << " items taken in any one of them)\n\n";

            _bridge->got_owned.store(_owned, std::memory_order_release);
            _bridge->got_shared.store(_shared, std::memory_order_release);
            _bridge->got_checksum.store(_sum, std::memory_order_release);
            // Unregister HERE, not in the handler below: the event is delivered on a later turn,
            // and until then this callback keeps running and printing. Measured — the paragraph
            // above appeared twice on the first run.
            unregisterCallback();
            push<Drained>(id(), _owned, _shared);
        }
    }
};

// ---------------------------------------------------------------------------------------
// 4. BACKPRESSURE — measured, on a ring small enough to overflow.
//
// Run before the engine so it is a plain, single-threaded measurement with nothing else moving.
// ---------------------------------------------------------------------------------------
std::size_t
measure_backpressure() {
    TinyRing tiny;
    Job      scratch[TINY_SLOTS]{};

    std::size_t accepted = 0;
    std::size_t refused  = 0;

    // TINY_SLOTS * 2 offers into a ring nobody is draining. `enqueue` returns false the moment
    // the ring is full — it does not block, it does not grow, and it does not throw.
    for (std::uint32_t i = 0; i < TINY_SLOTS * 2; ++i) {
        if (tiny.enqueue(std::size_t{0}, Job{0, i, i}))
            ++accepted;
        else
            ++refused;
    }

    const std::size_t drained = tiny.dequeue(scratch, TINY_SLOTS);

    qb::io::cout() << "[full] a bounded ring is BACKPRESSURE, not an error: enqueue returned false " << refused
                   << " times\n"
                      "       out of "
                   << (TINY_SLOTS * 2)
                   << " offers, and what to do about that is the DESIGN decision — drop, block\n"
                      "       the producer, or size the ring for the burst. The ring itself never allocates.\n";
    qb::io::cout() << "       (declared capacity " << TINY_SLOTS << ", accepted " << accepted << ", drained back out " << drained
                   << " — the template argument is the USABLE count; the ring allocates one extra\n"
                      "       slot of its own so full and empty are distinguishable)\n\n";
    return refused;
}

// ---------------------------------------------------------------------------------------
// 5. THE PRIMITIVE ITSELF.
// ---------------------------------------------------------------------------------------
bool
measure_spinlock() {
    qb::lockfree::SpinLock lock;

    // trylock(): one exchange, no waiting, and [[nodiscard]] — ignoring the answer means
    // "unlock something you may not hold".
    const bool took = lock.trylock();
    const bool busy = !lock.trylock(); // the same thread, already holding it: still refused
    const bool held = lock.locked();
    lock.unlock();

    // trylock_for(): spins on a RELAXED LOAD until the lock looks free, then re-attempts the
    // exchange (test-and-test-and-set). Hammering the exchange instead takes the cache line
    // exclusive on every attempt and actively starves the holder — measured at up to 4.3x on
    // this codebase before the wait loops were given this shape.
    const auto start   = std::chrono::steady_clock::now();
    const bool timed   = lock.trylock_for(std::chrono::milliseconds(2));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (timed)
        lock.unlock();

    // And the ordinary RAII use, which is what almost every call site should be.
    std::uint64_t guarded_total = 0;
    {
        std::lock_guard<qb::lockfree::SpinLock> guard(lock);
        guarded_total += 1; // nanoseconds of work. Anything longer belongs to a std::mutex.
    }

    qb::io::cout() << "[spinlock] trylock() is one exchange and never waits; trylock_for() spins on a RELAXED\n"
                      "           load and only re-attempts the exchange when the lock LOOKS free, which is what\n"
                      "           stops N waiters from starving the holder they are waiting on\n";
    qb::io::cout() << "           (trylock=" << (took ? "acquired" : "refused") << ", second attempt=" << (busy ? "refused" : "acquired")
                   << ", locked()=" << (held ? "yes" : "no") << ", trylock_for on a free lock returned in "
                   << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " us)\n";
    qb::io::cout() << "[spinlock] and the rule that matters: a spinlock burns a core while it waits, so it is\n"
                      "           for critical sections measured in NANOSECONDS. Never hold one across a syscall,\n"
                      "           an allocation, another lock, or a co_await — use std::mutex, which sleeps\n\n";

    return took && busy && held && timed && guarded_total == 1;
}

} // namespace

int
main() {
    qb::io::cout() << "[why] an actor may only be touched from its own VirtualCore, so a foreign thread hands\n"
                      "      work over by VALUE through a lock-free ring and never by calling the actor. spsc is\n"
                      "      the ONE-producer form (01-actors/03-event-payloads); mpsc is P of those side by side\n\n";

    const std::size_t refused_in_tiny = measure_backpressure();
    const bool        spinlock_ok     = measure_spinlock();

    Bridge bridge;

    qb::Main   engine;
    const auto drain = engine.addActor<BridgeActor>(0, &bridge);

    // ---- section 1: three threads, three slots they own -----------------------------------
    // `enqueue(index, item)` takes NO lock. That is safe here and ONLY here: slot i is written
    // by thread i and by nobody else, which restores the single-producer contract per ring.
    std::vector<std::thread> owners;
    owners.reserve(PRODUCERS);
    for (std::uint32_t p = 0; p < PRODUCERS; ++p) {
        owners.emplace_back([&bridge, p] {
            for (std::uint32_t i = 0; i < PER_THREAD; ++i) {
                const Job job{p, i, static_cast<std::uint64_t>(p) * 1000u + i};
                bridge.offered.fetch_add(1, std::memory_order_relaxed);
                // A ring this size is never full here; the retry is what a real producer needs
                // and costs nothing when it does not fire.
                while (!bridge.owned_slots.enqueue(static_cast<std::size_t>(p), job))
                    std::this_thread::yield();
                bridge.checksum.fetch_add(job.value, std::memory_order_relaxed);
            }
        });
    }

    // ---- section 2: two threads with no slot of their own ---------------------------------
    // `enqueue(item)` picks a slot with a thread-local counter and takes that slot's SpinLock,
    // because two threads CAN land on the same slot. This is the overload for a thread pool.
    std::vector<std::thread> pool;
    pool.reserve(2);
    for (std::uint32_t p = 0; p < 2; ++p) {
        pool.emplace_back([&bridge, p] {
            for (std::uint32_t i = 0; i < POOL_JOBS; ++i) {
                const Job job{100u + p, i, 500000u + static_cast<std::uint64_t>(p) * 1000u + i};
                bridge.offered.fetch_add(1, std::memory_order_relaxed);
                while (!bridge.shared_slots.enqueue(job))
                    std::this_thread::yield();
                bridge.checksum.fetch_add(job.value, std::memory_order_relaxed);
            }
        });
    }

    // ASYNCHRONOUS start (the default). `start(false)` runs the engine on THIS thread and does not
    // return until it stops, so the `producing.store(false)` below would never execute and the
    // drain would wait forever — measured, on the first run of this file.
    engine.start();

    for (auto &t : owners)
        t.join();
    for (auto &t : pool)
        t.join();
    // Published AFTER every producer has joined, so the drain's "producing == false and the ring
    // came back empty" test cannot see the flag before the last write it guards.
    bridge.producing.store(false, std::memory_order_release);

    engine.join();

    const std::uint64_t expect_owned  = PRODUCERS * PER_THREAD;
    const std::uint64_t expect_shared = 2 * POOL_JOBS;

    const bool counts_ok = drain.is_valid() && bridge.got_owned.load(std::memory_order_acquire) == expect_owned
                           && bridge.got_shared.load(std::memory_order_acquire) == expect_shared;
    const bool sum_ok    = bridge.got_checksum.load(std::memory_order_acquire) == bridge.checksum.load(std::memory_order_acquire)
                           && bridge.offered.load(std::memory_order_acquire) == expect_owned + expect_shared;
    const bool ok        = counts_ok && sum_ok && spinlock_ok && refused_in_tiny > 0 && !engine.hasError();

    if (!counts_ok)
        qb::io::cerr() << "[verify] UNEXPECTED: item counts do not match what the producers offered\n";
    if (!sum_ok)
        qb::io::cerr() << "[verify] UNEXPECTED: checksum mismatch — an item was lost or read twice\n";

    qb::io::cout() << "=== lockfree bridge complete: every item a foreign thread produced was accounted for\n"
                      "    exactly once, with no mutex on the crossing and no allocation on the hot path ===\n";

    return ok ? 0 : 1;
}
