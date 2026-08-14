/**
 * @file examples/04-patterns/01-pubsub.cpp
 * @tier 04-patterns
 * @teaches A publish/subscribe bus you do not write: `qb::PubSub<Topic>` is a per-core
 *          ServiceActor, so a publisher reaches every subscriber on its own core with no
 *          registry, no ids plumbed through constructors and no cleanup code — and a
 *          subscriber killed without unsubscribing is inert rather than a leak.
 * @demonstrates qb::PubSub<PriceTick>, getService, subscribe, unsubscribe, publish,
 *               subscriber_count, tracked_slot_count,
 *               registerEvent<E>, push<E>, kill(), getIndex(), qb::Main, addActor<T>,
 *               qb::string<8>
 * @prerequisites 01-actors/04-cores-and-placement
 * @expect "[main] one PubSub<PriceTick> per core, three desks, two publishers"
 * @expect "[bus@0] both desks subscribed, subscriber_count="
 * @expect "[bus@1] its one desk subscribed, subscriber_count="
 * @expect " from the other core's bus"
 * @expect "[bus@0] a KILLED subscriber leaves its id behind: subscriber_count="
 * @expect "[bus@1] a POLITE subscriber reclaims its slot at once: "
 * @expect "=== pub/sub complete: 2 buses, 3 desks, no registry code ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * `examples/core/example7_pub_sub.cpp` is 969 lines that hand-roll this — and at line 64 it
 * names `qb::PubSub` in a comment and then does not use it. The hand-rolled version keeps its
 * own subscriber map, has no way to notice a dead subscriber, and (measured by the example
 * audit) ran its whole demo eighteen times concurrently. This file is the same lesson in a
 * fifth of the code, and the parts it does not write are the parts that were wrong.
 *
 * THE THREE THINGS THAT ARE EASY TO GET WRONG
 * -------------------------------------------
 * 1. SUBSCRIBING IS NOT ENOUGH. A subscriber must ALSO `registerEvent<Topic>(*this)`. The bus
 *    pushes the topic event to every subscribed id; an actor with no handler for that type
 *    receives nothing, silently. Subscription says *who*, registration says *what*.
 * 2. THE BUS IS PER CORE. `qb::PubSub<Topic>` is a `ServiceActor`, and a service is one
 *    instance per `VirtualCore`. `publish()` reaches the subscribers of the bus you called it
 *    on and nobody else. There are two cores here with one bus each, and the core-1 desk never
 *    sees a core-0 symbol. That is the design, not a limitation to route around: a fan-out
 *    that crossed cores would be one cross-core push per subscriber, and that is a thing to
 *    write deliberately (a relay actor per core) rather than to get by accident.
 * 3. A KILLED SUBSCRIBER LEAVES ITS ID BEHIND, AND THAT IS FINE. It never got to call
 *    `unsubscribe()`, so its id stays in the list until something reclaims it — but it is
 *    inert: the router finds no handler for a dead id, so nothing is delivered, and
 *    `subscriber_count()` filters by liveness so it is not counted either.
 *    `tracked_slot_count()` is the bookkeeping number, `subscriber_count()` is the delivery
 *    number, and this program prints both at the one moment they disagree.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-pubsub
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-pubsub
 */

#include <string_view>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>
#include <qb/string.h>

// The published topic. `qb::string<8>` and not `std::string`: the engine relocates an event with
// memcpy and never runs the source destructor, so a payload member may hold no pointer into
// itself — which a short std::string does on libstdc++. Bounded strings are the sanctioned shape.
struct PriceTick : public qb::Event {
    qb::string<8> symbol;
    int           cents;
    bool          last; ///< ends a publication wave, so a desk knows when to report

    PriceTick(std::string_view sym, int c, bool l)
        : symbol(sym)
        , cents(c)
        , last(l) {}
};

// Desk -> its local feed: "I have registered a handler and subscribed."
struct DeskReady : public qb::Event {};

// Desk -> the reporter: how many ticks that desk saw in the wave that just ended.
struct DeskReport : public qb::Event {
    qb::string<16> desk;
    int            received;
    DeskReport(std::string_view d, int n)
        : desk(d)
        , received(n) {}
};

// Reporter -> a desk: leave the bus. `polite` picks which of the two exits it takes.
struct Leave : public qb::Event {
    bool polite;
    explicit Leave(bool p)
        : polite(p) {}
};

// A departing desk -> its own feed: "I am gone; look at what the bus makes of that."
struct Left : public qb::Event {
    bool polite;
    explicit Left(bool p)
        : polite(p) {}
};

// ---------------------------------------------------------------------------
// A subscriber. Joining the bus is two lines of onInit; leaving it is one line, or none.
// ---------------------------------------------------------------------------
class Desk : public qb::Actor {
    qb::string<16> _name;
    qb::ActorId    _feed;
    qb::ActorId    _reporter;
    int            _received = 0;
    int            _foreign  = 0; ///< ticks carrying the other core's symbol — must stay 0
    int            _wave     = 0;

public:
    Desk(std::string_view name, qb::ActorId feed, qb::ActorId reporter)
        : _name(name)
        , _feed(feed)
        , _reporter(reporter) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<PriceTick>(*this); // WHAT this desk can receive
        registerEvent<Leave>(*this);
        getService<qb::PubSub<PriceTick>>()->subscribe(id()); // WHO the bus should reach
        push<DeskReady>(_feed);
        co_return true;
    }

    void
    on(PriceTick const &t) {
        ++_received;
        // The per-core boundary, measured rather than asserted: core 0 publishes AAPL and core 1
        // publishes BTC-EUR, so a symbol from the other core arriving here would be a crossing.
        const auto sym = std::string_view{t.symbol};
        if ((sym == "AAPL" && getIndex() != 0) || (sym == "BTC-EUR" && getIndex() != 1))
            ++_foreign;
        if (!t.last)
            return;
        qb::io::cout() << "[desk " << _name.c_str() << "] wave " << ++_wave << " complete: " << _received << " ticks, " << _foreign
                       << " from the other core's bus\n";
        push<DeskReport>(_reporter, std::string_view{_name}, _received);
    }

    void
    on(Leave const &e) {
        if (e.polite) {
            // The cheapest and most deterministic exit — available only to an actor that knows
            // it is going away.
            getService<qb::PubSub<PriceTick>>()->unsubscribe(id());
            qb::io::cout() << "[desk " << _name.c_str() << "] unsubscribed, then killed itself\n";
        } else {
            // The exit most actors actually take: killed by somebody else, no chance to leave.
            qb::io::cout() << "[desk " << _name.c_str() << "] killed WITHOUT unsubscribing\n";
        }
        // Pushed before kill(): this handler still runs to its end, and `kill()` only marks the
        // actor dead — it does not return from the function that calls it.
        push<Left>(_feed, e.polite);
        kill();
    }
};

// ---------------------------------------------------------------------------
// A publisher. It holds no subscriber list of its own — that is the entire point.
// ---------------------------------------------------------------------------
class MarketFeed : public qb::Actor {
    qb::string<8> _symbol;
    int           _expect_desks;
    int           _ready = 0;

public:
    MarketFeed(std::string_view symbol, int expect_desks)
        : _symbol(symbol)
        , _expect_desks(expect_desks) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<DeskReady>(*this);
        registerEvent<Left>(*this);
        co_return true;
    }

    void
    on(DeskReady const &) {
        if (++_ready < _expect_desks)
            return;
        auto *bus = getService<qb::PubSub<PriceTick>>();
        if (getIndex() == 0)
            qb::io::cout() << "[bus@0] both desks subscribed, subscriber_count=" << bus->subscriber_count() << "\n";
        else
            qb::io::cout() << "[bus@1] its one desk subscribed, subscriber_count=" << bus->subscriber_count() << "\n";
        publish_wave(*bus, 3);
    }

    // The departed subscriber is already dead by the time this arrives: `kill()` marks the actor
    // dead inside the handler that called it, and this event was pushed just before that call.
    // In this program the polite desk is the one on core 1 and the killed one is on core 0, which
    // is why each branch can name its core.
    void
    on(Left const &e) {
        auto *bus = getService<qb::PubSub<PriceTick>>();
        if (e.polite)
            qb::io::cout() << "[bus@1] a POLITE subscriber reclaims its slot at once: "
                              "subscriber_count="
                           << bus->subscriber_count() << " tracked_slot_count=" << bus->tracked_slot_count() << "\n";
        else
            qb::io::cout() << "[bus@0] a KILLED subscriber leaves its id behind: subscriber_count=" << bus->subscriber_count()
                           << " tracked_slot_count=" << bus->tracked_slot_count() << "\n";
        // Either way, publishing again is correct and safe: a dead id resolves to no handler, and
        // publishing into an empty bus is a well-defined no-op rather than an error.
        publish_wave(*bus, 1);
    }

private:
    void
    publish_wave(qb::PubSub<PriceTick> &bus, int n) {
        for (int i = 1; i <= n; ++i)
            bus.publish(std::string_view{_symbol}, 10000 + i, i == n); // builds each PriceTick
    }
};

// ---------------------------------------------------------------------------
// Drives the two exits and ends the run. It never touches the bus, and it is handed no desk
// id: it learns each desk from that desk's own report. That is what pub/sub spares you.
// ---------------------------------------------------------------------------
class Reporter : public qb::Actor {
    qb::ActorId _killed_desk;
    qb::ActorId _polite_desk;
    int         _reports = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<DeskReport>(*this);
        co_return true;
    }

    void
    on(DeskReport const &r) {
        ++_reports;
        if (std::string_view{r.desk} == "equities-B")
            _killed_desk = r.getSource();
        if (std::string_view{r.desk} == "crypto-C")
            _polite_desk = r.getSource();
        if (_reports == 3) { // equities-A + equities-B on core 0, crypto-C on core 1
            push<Leave>(_killed_desk, false);
            push<Leave>(_polite_desk, true);
            return;
        }
        if (_reports > 3) {
            // The survivor reported a second wave: the bus still delivers with a dead id in it.
            qb::io::cout() << "=== pub/sub complete: 2 buses, 3 desks, no registry code ===\n";
            qb::Main::stop();
        }
    }
};

int
main() {
    qb::Main engine;

    // The bus goes in FIRST on each core: a subscriber resolves it inside its own onInit, and
    // `getService<T>()` hands back a service even while that service is still Activating — but
    // it cannot hand back one that was never added.
    engine.addActor<qb::PubSub<PriceTick>>(0);
    engine.addActor<qb::PubSub<PriceTick>>(1);

    auto reporter = engine.addActor<Reporter>(0);
    auto feed0    = engine.addActor<MarketFeed>(0, "AAPL", 2);
    auto feed1    = engine.addActor<MarketFeed>(1, "BTC-EUR", 1);

    engine.addActor<Desk>(0, "equities-A", feed0, reporter);
    engine.addActor<Desk>(0, "equities-B", feed0, reporter);
    engine.addActor<Desk>(1, "crypto-C", feed1, reporter);

    qb::io::cout() << "[main] one PubSub<PriceTick> per core, three desks, two publishers\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
