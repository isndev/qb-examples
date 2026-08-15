/**
 * @file examples/01-actors/03-event-payloads.cpp
 * @tier 01-actors
 * @teaches The one rule about event payloads you cannot discover by testing on a Mac: an event
 *          is RELOCATED with memcpy and its source destructor is never run, so no member may
 *          STORE a pointer into itself. Which shapes are safe, which are not, why the compiler
 *          cannot tell you, what the debug guard does — and how a foreign thread feeds an
 *          actor system without a mutex.
 * @demonstrates qb::Event, qb::string<32>, qb::ActorId, qb::WithData<int>, qb::FillEvent<int>,
 *               qb::lockfree::spsc::ringbuffer<Sample, 1024>, qb::ICallback,
 *               qb::LoopEvent, registerCallback, unregisterCallback, registerEvent<E>, push<E>,
 *               broadcast<qb::KillEvent>, qb::KillEvent, getIndex, qb::Main, addActor<T>
 * @prerequisites 01-actors/02-messaging
 * @expect "[measure] qb::string<32> is trivially copyable: an array plus a length, nothing to dangle"
 * @expect "[measure] a SHORT std::string keeps its characters INSIDE the object <- the shape to avoid"
 * @expect "[measure] a LONG std::string keeps its characters on the heap, and you cannot tell which"
 * @expect "[rule] safe on this event: PODs, qb::string<N>, qb::ActorId, shared_ptr, vector"
 * @expect "] payload survived the crossing: id="
 * @expect "] boxed body (shared_ptr) still readable, "
 * @expect "[bridge] a foreign thread produced "
 * @expect "[bridge] the actor consumed "
 * @expect "=== payloads complete: nothing crossed a core boundary holding a pointer to itself ==="
 *
 * WHAT THE ENGINE ACTUALLY DOES
 * -----------------------------
 * A cross-core `push` copies the event's bytes into the destination core's mailbox with
 * `memcpy` (`qb/src/qb/core/Main.cpp:215`) and reclaims the source slot WITHOUT running a
 * destructor. The receiving core then `reinterpret_cast`s those bytes back into your type. So
 * the event is not moved and it is not copy-constructed: it is **relocated**.
 *
 * That is also not a cross-core-only concern. Pipe growth, pipe compaction, `reply()` and
 * `forward()` relocate same-core events the same way. Assume every event may move.
 *
 * THE RULE, AND WHY THERE IS NO static_assert FOR IT
 * --------------------------------------------------
 * A payload member may not STORE a pointer into itself, because the stored address still names
 * the old bytes after the relocation. C++20 has no `is_trivially_relocatable`, and the nearest
 * available trait — `is_trivially_copyable` — is far too strict: it would reject
 * `std::shared_ptr` and `std::vector`, both of which are perfectly safe here because they point
 * at the HEAP, not at themselves. So the framework declines to assert (`Main.cpp:148-151`) and
 * ships a **debug-only** guard instead: `event_points_into_itself` (`Main.cpp:173-187`) scans
 * the event's own byte range for a pointer aimed back into it, and on a hit logs `QB_LOG_CRIT`
 * and asserts. It exists in Debug builds only, and it relies on the Debug-only zeroing in
 * `qb::detail::prepare_event_storage` — do not remove one without the other.
 *
 * WHY A MAC WILL NOT SHOW YOU THE BUG
 * -----------------------------------
 * The classic violation is a SHORT `std::string` by value. Both standard libraries keep short
 * strings inside the object; the difference is whether the data pointer is STORED or DERIVED.
 * libstdc++ stores `_M_p` pointing at its own inline buffer, so after a relocation it still
 * addresses the old storage — memory corruption. libc++ recomputes the pointer from `this`, so
 * the same code is silently fine. This corpus has already paid for that asymmetry once: a
 * payload defect that was invisible on macOS and corrupted on Linux. The measurement section
 * below prints what YOUR standard library does, so the difference is a number rather than a
 * paragraph you have to trust.
 *
 * THE FIVE NAMES YOU MAY NOT USE
 * ------------------------------
 * `qb::Event` keeps its routing header in PRIVATE members named `id`, `dest`, `source`,
 * `bucket_size` and `state`. A derived event that declares any of those HIDES the base field —
 * and until 3.0 that compiled clean and silently corrupted routing. It is now a compile error
 * naming the field and the type (`Event.h:838-880`); the line is left below, commented out,
 * because a compile error cannot be demonstrated at runtime.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-event-payloads
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-event-payloads
 */

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/string.h>
#include <qb/system/lockfree/spsc.h>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------------------
// 1. THE MEASUREMENT — no engine, no actor, no undefined behaviour.
//
// Nothing here relocates anything. It only asks each candidate payload type the two questions
// that decide whether relocating it would be safe.
// ---------------------------------------------------------------------------------------

/// Does `p` address a byte inside `obj`? `std::less` on `const void*` is the one comparison the
/// standard gives a total order over unrelated pointers, which is exactly this case.
template <typename T>
bool
points_inside(T const &obj, void const *p) noexcept {
    auto const *lo = static_cast<void const *>(&obj);
    auto const *hi = static_cast<void const *>(reinterpret_cast<char const *>(&obj) + sizeof(T));
    return !std::less<void const *>{}(p, lo) && std::less<void const *>{}(p, hi);
}

void
measure_payload_shapes() {
    // Each verdict below is a WHOLE sentence chosen by the measurement, not a value spliced
    // into one. That is what lets the example runner assert the finding rather than merely
    // assert that this function was reached.
    qb::io::cout() << (std::is_trivially_copyable_v<qb::string<32>>
                           ? "[measure] qb::string<32> is trivially copyable: an array plus a length, nothing to dangle\n"
                           : "[measure] qb::string<32> is NOT trivially copyable — that would be a framework regression\n");
    qb::io::cout() << "[measure] std::is_trivially_copyable_v<std::string> = " << (std::is_trivially_copyable_v<std::string> ? "yes" : "no")
                   << ", <std::vector<int>> = " << (std::is_trivially_copyable_v<std::vector<int>> ? "yes" : "no")
                   << ", <std::shared_ptr<int>> = " << (std::is_trivially_copyable_v<std::shared_ptr<int>> ? "yes" : "no") << "\n";
    qb::io::cout() << "          ...and the last two are SAFE anyway: the trait is too strict to be the rule\n";

    const std::string      short_str = "short";
    const std::string      long_str  = "a string long enough that no implementation keeps it inline";
    const std::vector<int> vec{1, 2, 3};
    const auto             boxed = std::make_shared<int>(42);

    qb::io::cout() << (points_inside(short_str, short_str.data())
                           ? "[measure] a SHORT std::string keeps its characters INSIDE the object <- the shape to avoid\n"
                           : "[measure] a SHORT std::string did NOT point into itself on this library\n");
    qb::io::cout() << (points_inside(long_str, long_str.data())
                           ? "[measure] a LONG std::string pointed into itself, which no implementation should do\n"
                           : "[measure] a LONG std::string keeps its characters on the heap, and you cannot tell which\n"
                             "          one you have at compile time — which is why the rule is about the TYPE\n");
    qb::io::cout() << "[measure] std::vector points at the heap: " << (points_inside(vec, vec.data()) ? "no" : "yes")
                   << "; shared_ptr points at the heap: " << (points_inside(boxed, boxed.get()) ? "no" : "yes") << " — both fine to relocate\n";
    qb::io::cout() << "[rule] safe on this event: PODs, qb::string<N>, qb::ActorId, shared_ptr, vector. "
                      "Unsafe: std::string by value, and anything else that stores a pointer into itself\n\n";
}

// ---------------------------------------------------------------------------------------
// 2. THE EVENT — every member of it chosen against the rule above.
// ---------------------------------------------------------------------------------------
struct Reading : qb::Event {
    // A POD. Nothing to say.
    std::uint32_t sequence{0};

    // A BOUNDED string, stored inline as an array. Overflow TRUNCATES silently rather than
    // throwing or corrupting — `qb::string` clamps every write against its free room — so size
    // the N for your data, not for your optimism.
    qb::string<32> sensor;

    // An id is a trivially-copyable 32-bit integer, so carrying one is always safe. This is why
    // "who should I answer" travels as an ActorId and not as a pointer.
    qb::ActorId origin;

    // UNBOUNDED data goes in a box. The control block and the buffer are on the heap; the
    // event carries only the two words of the shared_ptr, and the refcount survives the
    // relocation because nothing about it points into the event.
    //
    // RELOCATABLE IS NOT THE SAME AS OWNED, and this is the half of the shared_ptr rule that
    // costs people a week. Boxing settles whether the EVENT can be memcpy'd; it says nothing
    // about who may touch the POINTEE. If the sender keeps its own copy of this handle and
    // then writes through it while the recipient — on another core, on another thread — reads
    // through it, that is an ordinary data race, and the box is what made it convenient. A
    // shared_ptr in an event means "here, take it", not "here, we both have it": send a
    // snapshot per recipient, or do not keep a copy. ThreadSanitizer is the instrument that
    // sees this one; a release build will not.
    std::shared_ptr<std::vector<double>> samples;

    // A member named `id`, `dest`, `source`, `bucket_size` or `state` HIDES a private routing
    // field of qb::Event. Uncomment to see the compile error, which names both the field and
    // this type:
    //     int id{0};

    Reading(std::uint32_t seq, std::string_view name, qb::ActorId who, std::shared_ptr<std::vector<double>> data)
        : sequence(seq)
        , sensor(name)
        , origin(who)
        , samples(std::move(data)) {}
};

// The two shipped envelopes, so a reader knows they exist. `qb::WithData<Ts...>` is an event
// carrying a `std::tuple<Ts...> data`; `qb::FillEvent<Ts...>` is the same thing plus a default
// constructor. Both take their arguments as true rvalue references, not forwarding references.
struct Tally : qb::FillEvent<int> {
    using base_t = qb::FillEvent<int>;
    using base_t::base_t;
};
// `FillEvent<Ts...>` IS a `WithData<Ts...>` plus a default constructor; `WithData` alone has
// none, which is the whole reason the second name exists.
static_assert(std::is_base_of_v<qb::WithData<int>, Tally>);

// Sent by the consumer once it has seen everything the producer sent, so the shutdown below is
// driven by a COUNT rather than by a duration.
struct Done : qb::Event {};

// A trivially-copyable sample: the ONLY thing an spsc ring will carry, by static_assert.
struct Sample {
    std::uint64_t at_ns;
    double        value;
};

using SampleRing = qb::lockfree::spsc::ringbuffer<Sample, 1024>;

// ---------------------------------------------------------------------------------------
// 3. THE CROSSING — core 0 builds it, core 1 reads it back.
// ---------------------------------------------------------------------------------------
class Consumer : public qb::Actor {
    qb::ActorId _bridge;

public:
    explicit Consumer(qb::ActorId bridge)
        : _bridge(bridge) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Reading>(*this);
        registerEvent<Tally>(*this);
        co_return true;
    }

    void
    on(Reading &r) {
        // These bytes were memcpy'd out of another core's pipe. Every member below is readable
        // because none of them held an address inside the event.
        qb::io::cout() << "[core " << getIndex() << "] payload survived the crossing: id=" << r.sequence << " sensor='" << r.sensor
                       << "' origin=" << static_cast<std::uint32_t>(r.origin) << "\n";
        qb::io::cout() << "[core " << getIndex() << "] boxed body (shared_ptr) still readable, " << r.samples->size() << " samples, first "
                       << r.samples->front() << " — the vector never moved; only the two words of the handle did\n";
    }

    void
    on(Tally &t) {
        qb::io::cout() << "[core " << getIndex() << "] qb::FillEvent<int> carried " << std::get<0>(t.data) << " in its tuple\n";
        // `Tally` is pushed second on the same pipe, so seeing it means `Reading` has already
        // been handled. Ordering is what `push` guarantees; `send` would not.
        push<Done>(_bridge);
    }
};

class Producer : public qb::Actor {
    qb::ActorId _consumer;

public:
    explicit Producer(qb::ActorId consumer)
        : _consumer(consumer) {}

    qb::io::async::task<bool>
    onInit() override {
        auto body = std::make_shared<std::vector<double>>(std::vector<double>{1.5, 2.5, 3.5});

        // A name longer than 32 characters would be TRUNCATED here, silently. That is the
        // trade for an inline string: it can never dangle, and it can never grow.
        push<Reading>(_consumer, 7u, "temp/room-3", id(), body);
        push<Tally>(_consumer, 99);
        co_return true;
    }
};

// ---------------------------------------------------------------------------------------
// 4. THE FOREIGN THREAD — the sanctioned bridge, and it is not a mutex.
//
// Actors never share mutable state; a thread that is not a VirtualCore worker may not touch an
// actor at all. The supported crossing is a single-producer/single-consumer lock-free ring
// drained by an `ICallback`, which runs once per loop pass on the actor's own thread.
// ---------------------------------------------------------------------------------------
class Bridge
    : public qb::Actor
    , public qb::ICallback {
    SampleRing        *_ring;
    std::atomic<bool> *_producing;
    std::size_t        _consumed      = 0;
    bool               _crossing_done = false;

public:
    Bridge(SampleRing *ring, std::atomic<bool> *producing)
        : _ring(ring)
        , _producing(producing) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Done>(*this);
        // `ICallback` is the EVERY-TURN hook, not a timer. It is the right tool here precisely
        // because draining must happen on every pass and must never block.
        registerCallback(*this);
        co_return true;
    }

    void
    on(Done &) {
        _crossing_done = true;
    }

    // Runs on this actor's core, once per loop iteration. Requirements: fast, and never
    // blocking — no mutex, no syscall, no sleep.
    void
    on(qb::LoopEvent const &) final {
        Sample s{};
        // The single-element `dequeue` is used deliberately. `consume_all` walks the ring IN
        // PLACE and invokes its functor TWICE across a wrap, which is a bug the moment one
        // logical item spans more than one slot — the framework's own mailbox records that
        // trap. One-at-a-time is always correct.
        while (_ring->dequeue(&s))
            ++_consumed;

        if (_crossing_done && !_producing->load(std::memory_order_acquire) && _ring->empty()) {
            qb::io::cout() << "[bridge] the actor consumed " << _consumed
                           << " samples from a foreign thread with no mutex, no shared actor state, "
                              "and no allocation on the hot path\n";
            unregisterCallback(); // stop being called before we go away
            qb::io::cout() << "\n=== payloads complete: nothing crossed a core boundary holding a pointer to itself ===\n";
            broadcast<qb::KillEvent>();
        }
    }
};

int
main() {
    measure_payload_shapes();

    // The ring and its flag outlive the engine, so the producer thread can be joined after
    // `join()` returns. Both are ordinary stack objects — the ring is the only thing the two
    // threads share, and it is lock-free by construction.
    SampleRing        ring;
    std::atomic<bool> producing{true};

    qb::Main engine;
    auto     bridge   = engine.addActor<Bridge>(0, &ring, &producing);
    auto     consumer = engine.addActor<Consumer>(1, bridge); // another core: the events really cross
    engine.addActor<Producer>(0, consumer);

    // A thread that knows nothing about actors. `Sample` is trivially copyable, which the ring
    // asserts: `spsc::ringbuffer<T, N>` uses memcpy for its bulk paths and refuses any other T.
    std::thread feed([&ring, &producing] {
        std::size_t sent = 0;
        for (std::size_t i = 0; i < 500; ++i) {
            // A full ring returns false rather than blocking or growing. Back-pressure is the
            // caller's problem, which is the honest design for a bounded queue.
            while (!ring.enqueue(Sample{static_cast<std::uint64_t>(i), static_cast<double>(i) * 0.5}))
                std::this_thread::yield();
            ++sent;
        }
        qb::io::cout() << "[bridge] a foreign thread produced " << sent << " samples into a lock-free spsc ring\n";
        producing.store(false, std::memory_order_release);
    });

    engine.start();
    engine.join();
    feed.join();
    return engine.hasError() ? 1 : 0;
}
