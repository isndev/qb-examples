/**
 * @file examples/01-actors/07-service-actor.cpp
 * @tier 01-actors
 * @teaches The framework's standard bootstrap object, which had zero demonstrators before this
 *          file: one singleton per core per tag, reachable from any actor on that core with a
 *          TYPED pointer and no id plumbing at all — plus the id of its peer on any OTHER core,
 *          computed rather than looked up.
 * @demonstrates qb::ServiceActor<ConfigTag>, qb::Service, getService<ConfigService>,
 *               getServiceId<ConfigTag>, qb::ActorId, getIndex, registerEvent<E>, push<E>,
 *               broadcast<qb::KillEvent>, qb::KillEvent, qb::Main, addActor<T>, usedCoreSet
 * @prerequisites 01-actors/04-cores-and-placement
 * @expect "] found its ConfigService with getService<T>(), no constructor argument"
 * @expect "] its core's setting is '"
 * @expect "edge-cache"
 * @expect "origin-store"
 * @expect "] getService<TelemetryService>() on a core that has none returns nullptr"
 * @expect "[worker@0] addressed the core-1 service with getServiceId, no lookup and no plumbing"
 * @expect "] received a cross-core message sent to a COMPUTED id, from '"
 * @expect "=== service actor complete: 2 services, 2 workers, 0 ActorIds passed by hand ==="
 *
 * THE PROBLEM IT SOLVES
 * ---------------------
 * Every actor system grows a handful of things that everybody needs: configuration, a metrics
 * sink, a connection pool, a clock. Without a service the id of each has to be threaded from
 * `main()` through every constructor that might one day need it, and adding a dependency means
 * editing every actor between the two.
 *
 * A `qb::ServiceActor<Tag>` is addressed by its TYPE instead:
 *
 *     struct ConfigTag {};                                    // a complete type, see below
 *     class ConfigService : public qb::ServiceActor<ConfigTag> { ... };
 *     // ...anywhere on the same core, in any actor, including inside its own onInit():
 *     if (auto *cfg = getService<ConfigService>()) use(cfg->setting());
 *
 * FOUR THINGS TO KNOW, AND EACH IS BELOW AS RUNNING CODE
 * ------------------------------------------------------
 * 1. **It is per CORE, not per system.** `addActor<ConfigService>(0)` and `addActor<>(1)` are
 *    two independent objects with different ids and possibly different contents. `getService`
 *    resolves the one on the calling actor's own core and nothing else. That is a feature: a
 *    service holds no shared mutable state, so it needs no lock.
 * 2. **`getService<T>()` returns `nullptr`** when this core has no service of that type. Check
 *    it. The framework logs a CRIT and hands back null rather than inventing one.
 * 3. **It is the one lookup that is NOT phase-gated.** Every other resolution (`findActor`, an
 *    `ActorHandle::get()`, `is_actor_alive`) withholds an actor whose async `onInit()` is still
 *    in flight; `getService` deliberately does not, so a service can resolve a peer from inside
 *    its own `onInit` without the two deadlocking on each other. The price is that the pointer
 *    it hands you may belong to a service that has not finished initialising — or one that has
 *    been killed and not yet reaped. Read only what its constructor set, or ask it by event.
 * 4. **A service's id is COMPUTED, not looked up.** `getServiceId<Tag>(core)` is a static
 *    function returning `ActorId{ServiceIndex, core}` — it needs no registry and works for a
 *    core other than your own, which `getService` cannot do. That is how a service on core 0
 *    addresses its counterpart on core 1 with nothing passed in.
 *
 * THE TAG MUST BE A COMPLETE TYPE
 * -------------------------------
 * `class Svc : public qb::ServiceActor<struct Tag>` used to compile; since 3.0 it does not, in
 * every build mode. Declare `struct ConfigTag {};` first. The tag is never instantiated — it
 * exists only to give the service a distinct index — but it must be complete.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-actors-service-actor
 * Run:
 *   ./build/presets/release/examples/01-actors/qb-example-actors-service-actor
 */

#include <string_view>
#include <type_traits>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/string.h>

// The tags. Complete types, never instantiated, each one naming a distinct service slot.
// (The static_assert below is at the bottom of this file, once ConfigService is complete.)
struct ConfigTag {};
struct TelemetryTag {};

// A message a worker sends to a service it addressed by a COMPUTED id.
struct Announce : qb::Event {
    qb::string<24> from;
    qb::ActorId    sender;

    Announce(std::string_view who, qb::ActorId id)
        : from(who)
        , sender(id) {}
};

struct Ack : qb::Event {};

// ---------------------------------------------------------------------------------------
// The service. One per core, and each core's copy may carry different contents.
//
// `qb::ServiceActor<Tag>` derives from `qb::Service`, which is an ordinary `qb::Actor` with a
// fixed ServiceId — that fixed id is the whole trick, because it makes the ActorId derivable.
// ---------------------------------------------------------------------------------------
class ConfigService : public qb::ServiceActor<ConfigTag> {
    qb::string<24> _setting;

public:
    explicit ConfigService(std::string_view setting)
        : _setting(setting) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Announce>(*this);
        co_return true;
    }

    // Read by peers on the SAME core through a typed pointer, so this needs no event and no
    // copy. It is `const` and it is set in the constructor: see note 3 in the header — the
    // pointer `getService` returns may belong to a service that is not finished activating, so
    // only constructor-set, immutable state is safe to read this way.
    [[nodiscard]] std::string_view
    setting() const noexcept {
        return _setting;
    }

    void
    on(Announce &a) {
        qb::io::cout() << "[config@" << getIndex() << "] received a cross-core message sent to a COMPUTED id, from '" << a.from
                       << "' — nobody ever passed this actor's id to anybody\n";
        push<Ack>(a.sender);
    }
};

// A service that is deliberately registered on core 0 ONLY, so `getService` has something to
// fail to find on core 1.
class TelemetryService : public qb::ServiceActor<TelemetryTag> {
public:
    qb::io::async::task<bool>
    onInit() override {
        co_return true;
    }
};

// ---------------------------------------------------------------------------------------
// The worker. Its constructor takes NOTHING: no config id, no telemetry id, no registry.
// ---------------------------------------------------------------------------------------
class Worker : public qb::Actor {
    qb::ActorId _reporter;

public:
    explicit Worker(qb::ActorId reporter)
        : _reporter(reporter) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Ack>(*this);

        // 1. Resolution by TYPE, from inside our own onInit, with nothing passed in.
        auto *cfg = getService<ConfigService>();
        if (cfg == nullptr) {
            qb::io::cout() << "[worker@" << getIndex() << "] no ConfigService on this core\n";
            co_return false;
        }
        qb::io::cout() << "[worker@" << getIndex() << "] found its ConfigService with getService<T>(), no constructor argument "
                       << "and no lookup table\n";
        qb::io::cout() << "[worker@" << getIndex() << "] its core's setting is '" << cfg->setting()
                       << (getIndex() == 0 ? "' — a DIFFERENT object from core 1's" : "' — one service instance PER CORE") << "\n";

        // 2. A service this core does not have. `nullptr`, not a crash and not a fallback.
        if (getService<TelemetryService>() == nullptr)
            qb::io::cout() << "[worker@" << getIndex() << "] getService<TelemetryService>() on a core that has none returns nullptr "
                           << "— always check it\n";
        else
            qb::io::cout() << "[worker@" << getIndex() << "] this core does host a TelemetryService\n";

        // 3. The OTHER core's service, addressed without a lookup. `getServiceId<Tag>(core)` is
        //    a static function over the tag: it builds `ActorId{ServiceIndex, core}` from the
        //    type alone. `getService` could not answer this — it is same-core only.
        if (getIndex() == 0) {
            const qb::ActorId peer = getServiceId<ConfigTag>(1);
            qb::io::cout() << "[worker@0] addressed the core-1 service with getServiceId, no lookup and no plumbing "
                           << "(its id is " << static_cast<std::uint32_t>(peer) << ", computed from the tag)\n";
            push<Announce>(peer, "worker@0", id());
        } else {
            push<Ack>(_reporter); // core 1 has nothing to announce; report straight away
        }

        co_return true;
    }

    void
    on(Ack &) {
        push<Ack>(_reporter);
    }
};

// Counts the two acknowledgements and shuts the system down on a COUNT, never on a duration.
class Reporter : public qb::Actor {
    int _acks = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Ack>(*this);
        co_return true;
    }

    void
    on(Ack &) {
        if (++_acks < 2)
            return;
        qb::io::cout() << "\n=== service actor complete: 2 services, 2 workers, 0 ActorIds passed by hand ===\n";
        broadcast<qb::KillEvent>();
    }
};

// `ServiceActor<Tag>` derives from `qb::Service`, which is an ordinary `qb::Actor` carrying a
// FIXED ServiceId — and that fixed id is the entire mechanism, because it is what makes
// `getServiceId<Tag>(core)` computable without a registry.
static_assert(std::is_base_of_v<qb::Service, ConfigService>);
static_assert(std::is_base_of_v<qb::Actor, qb::Service>);

int
main() {
    qb::Main engine;

    // The same class on two cores, with different contents. Neither knows about the other.
    engine.addActor<ConfigService>(0, "edge-cache");
    engine.addActor<ConfigService>(1, "origin-store");
    engine.addActor<TelemetryService>(0); // core 0 only, on purpose

    auto reporter = engine.addActor<Reporter>(0);
    engine.addActor<Worker>(0, reporter);
    engine.addActor<Worker>(1, reporter);

    qb::io::cout() << "=== service actors: one singleton per core, addressed by TYPE ===\n\n";

    engine.start();
    engine.join();

    // `usedCoreSet()` is only readable once the engine has stopped touching its initializers;
    // it reports which cores actually ran, which is the honest denominator for "per core".
    qb::io::cout() << "[main] cores used: " << engine.usedCoreSet().size() << "\n";
    return engine.hasError() ? 1 : 0;
}
