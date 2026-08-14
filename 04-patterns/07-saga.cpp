/**
 * @file examples/04-patterns/07-saga.cpp
 * @tier 04-patterns
 * @teaches How to undo a multi-step operation that has no transaction to roll back. Each step
 *          registers its own compensation as soon as it succeeds; if a later step fails,
 *          `qb::run_saga` runs the registered compensations in REVERSE order and then re-throws
 *          — so exactly the steps that happened are the steps that get undone.
 * @demonstrates qb::run_saga, qb::SagaScope, on_compensate, pending, qb::ask, qb::answer,
 *               resolve_ask, qb::Request<int>, spawn, qb::ScopedCoroContext, registerEvent<E>,
 *               qb::Main, addActor<T>
 * @prerequisites 04-patterns/04-scatter-gather
 * @expect ": all steps succeeded, 2 compensations registered and none of them ran"
 * @expect "[saga] booking 2 failed at the LAST step; rollback ran in reverse: "
 * @expect "[saga] booking 3 failed at the FIRST step; compensations registered by then: "
 * @expect "=== saga complete: undo runs backwards, over the steps that happened ==="
 *
 * WHAT A SAGA IS FOR
 * ------------------
 * Three actors, three side effects, no shared database and therefore no `ROLLBACK`. A seat is
 * reserved, a card is charged, a ticket is issued — and if the ticket cannot be issued, the
 * charge must be refunded and the seat released, in that order, because the later step is the
 * one that is safest to undo first. Written by hand this is a pile of nested try/catch and a
 * variable per step remembering how far you got. `SagaScope` is that variable, and
 * `on_compensate` is the only thing you write.
 *
 * FOUR THINGS THE HEADER MEANS AND THE CODE MAKES VISIBLE
 * ------------------------------------------------------
 * 1. REGISTER THE UNDO AFTER THE DO. `on_compensate` is called once the step has succeeded, so
 *    a failure at step 1 has nothing registered and undoes nothing. Booking 3 proves it.
 * 2. FAILURE MEANS AN EXCEPTION. `qb::ask` throws on timeout and on cancel — nothing else. A
 *    responder that answers "declined" quickly has SUCCEEDED as far as the framework is
 *    concerned, so a business failure is yours to raise: check the response, then `throw`.
 *    (This is the same rule as `qb::answer`, whose `fn` must not throw: carry the failure in
 *    the payload and act on it in the caller.)
 * 3. ROLLBACK IS BEST-EFFORT. An exception thrown by one compensation is swallowed so the
 *    remaining ones still run. What you get back is the ORIGINAL failure, re-thrown after the
 *    rollback — not whatever went wrong while undoing.
 * 4. A KILL IS NOT A FAILURE. If the actor is killed mid-saga, `cancelled_error` propagates
 *    WITHOUT compensating: the process is going away and running undo work on a dying actor is
 *    worse than leaving it to whatever cleans up after a crash. If your compensations must run
 *    even then, they belong in a durable store, not in a coroutine frame.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-saga
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-saga
 */

#include <chrono>
#include <memory>
#include <stdexcept>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

using namespace std::chrono_literals;

// One exchange type per service. `undo` picks the direction: the compensation is the SAME
// conversation with the same responder, which is what keeps a saga readable.
struct SeatOp : qb::Request<int> {
    bool undo{false};
    int  seat{0};
};
struct MoneyOp : qb::Request<int> {
    bool undo{false};
    int  cents{0};
};
struct TicketOp : qb::Request<int> {
    int seat{0};
};

// A negative response means "declined". `ask` does not throw for it — the saga body does.
constexpr int kDeclined = -1;

// ---------------------------------------------------------------------------
// Three ordinary responders. None of them knows a saga exists.
// ---------------------------------------------------------------------------
class Inventory : public qb::Actor {
    int _reserves = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<SeatOp>(*this);
        co_return true;
    }

    void
    on(SeatOp &e) {
        if (e.undo)
            qb::io::cout() << "    [inventory] RELEASE seat " << e.seat << "\n";
        else if (++_reserves == 3) // the third booking finds no seat
            qb::io::cout() << "    [inventory] no seat available for booking 3\n";
        else
            qb::io::cout() << "    [inventory] reserve seat " << e.seat << "\n";
        const int reserves = _reserves;
        qb::answer(*this, e, [reserves](SeatOp const &r) { return (!r.undo && reserves == 3) ? kDeclined : r.seat; });
    }
};

class Payments : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<MoneyOp>(*this);
        co_return true;
    }

    void
    on(MoneyOp &e) {
        if (e.undo)
            qb::io::cout() << "    [payments] REFUND " << e.cents << "\n";
        else
            qb::io::cout() << "    [payments] charge " << e.cents << "\n";
        qb::answer(*this, e, [](MoneyOp const &r) { return r.cents; });
    }
};

class Ticketing : public qb::Actor {
    int _issued = 0;

public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<TicketOp>(*this);
        co_return true;
    }

    void
    on(TicketOp &e) {
        const bool decline = (++_issued == 2); // the second booking cannot be ticketed
        qb::io::cout() << (decline ? "    [ticketing] DECLINED for seat " : "    [ticketing] issue ticket for seat ") << e.seat << "\n";
        qb::answer(*this, e, [decline](TicketOp const &r) { return decline ? kDeclined : r.seat; });
    }
};

// ---------------------------------------------------------------------------
// The orchestrator. The saga body is the happy path, read top to bottom, with an undo hung off
// each step as it lands.
// ---------------------------------------------------------------------------
class BookingActor : public qb::Actor {
    qb::ActorId _inv;
    qb::ActorId _pay;
    qb::ActorId _tick;

public:
    BookingActor(qb::ActorId inv, qb::ActorId pay, qb::ActorId tick)
        : _inv(inv)
        , _pay(pay)
        , _tick(tick) {}

    qb::io::async::task<bool>
    onInit() override {
        // An asker registers EVERY exchange type it asks with — three services, three types.
        registerEvent<SeatOp>(*this);
        registerEvent<MoneyOp>(*this);
        registerEvent<TicketOp>(*this);

        auto inv  = _inv;
        auto pay  = _pay;
        auto tick = _tick;
        spawn([inv, pay, tick](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            for (int booking = 1; booking <= 3; ++booking) {
                qb::io::cout() << "[saga] booking " << booking << ":\n";
                // A shared_ptr and NOT a by-reference capture: the body below is stored inside
                // run_saga's own coroutine frame, and capturing a local by reference into a frame
                // that outlives the statement is the corpus's most expensive footgun.
                auto registered = std::make_shared<int>(0);
                try {
                    co_await qb::run_saga(
                        ctx, [inv, pay, tick, registered](qb::ScopedCoroContext ctx, qb::SagaScope &saga) -> qb::io::async::task<void> {
                            // --- step 1 -------------------------------------------------
                            auto seat = co_await qb::ask(ctx, inv, SeatOp{.undo = false, .seat = 12}, qb::duration{1s});
                            if (seat.response == kDeclined)
                                throw std::runtime_error("no seat");
                            saga.on_compensate([ctx, inv]() -> qb::io::async::task<void> {
                                (void) co_await qb::ask(ctx, inv, SeatOp{.undo = true, .seat = 12}, qb::duration{1s});
                            });

                            // --- step 2 -------------------------------------------------
                            auto paid = co_await qb::ask(ctx, pay, MoneyOp{.undo = false, .cents = 4999}, qb::duration{1s});
                            if (paid.response == kDeclined)
                                throw std::runtime_error("card refused");
                            saga.on_compensate([ctx, pay]() -> qb::io::async::task<void> {
                                (void) co_await qb::ask(ctx, pay, MoneyOp{.undo = true, .cents = 4999}, qb::duration{1s});
                            });

                            // --- step 3: nothing follows it, so it registers no undo ------
                            *registered = static_cast<int>(saga.pending());
                            auto t      = co_await qb::ask(ctx, tick, TicketOp{.seat = 12}, qb::duration{1s});
                            if (t.response == kDeclined)
                                throw std::runtime_error("ticketing declined");
                        });
                    qb::io::cout() << "[saga] booking " << booking
                                   << ": all steps succeeded, 2 compensations registered and none of them ran\n";
                } catch (std::runtime_error const &e) {
                    // What surfaces is the ORIGINAL failure, after the rollback has run.
                    if (booking == 2)
                        qb::io::cout() << "[saga] booking 2 failed at the LAST step; rollback ran in reverse: " << e.what() << "\n";
                    else
                        qb::io::cout() << "[saga] booking 3 failed at the FIRST step; compensations registered by then: " << *registered
                                       << ". Nothing to undo. Re-thrown: " << e.what() << "\n";
                }
            }
            qb::io::cout() << "=== saga complete: undo runs backwards, over the steps that happened ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    void
    on(SeatOp &e) {
        (void) resolve_ask(e);
    }
    void
    on(MoneyOp &e) {
        (void) resolve_ask(e);
    }
    void
    on(TicketOp &e) {
        (void) resolve_ask(e);
    }
};

int
main() {
    qb::Main engine;

    auto inv  = engine.addActor<Inventory>(0);
    auto pay  = engine.addActor<Payments>(0);
    auto tick = engine.addActor<Ticketing>(0);
    engine.addActor<BookingActor>(0, inv, pay, tick);

    qb::io::cout() << "[main] three services, three bookings: one succeeds, one fails last, one "
                      "fails first\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
