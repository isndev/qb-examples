/**
 * @file examples/03-coroutines/04-ask-request-response.cpp
 * @tier 03-coroutines
 * @teaches The one-to-one exchange: `co_await qb::ask(...)` replaces a correlation map, a reply
 *          handler and a timeout timer with one line — what the correlation id is, the single
 *          line you cannot leave out, and the two ways to get it wrong that produce a timeout
 *          and no other symptom whatsoever.
 * @demonstrates qb::ask, qb::Request<int>, qb::answer, resolve_ask,
 *               qb::io::async::timeout_error, qb::ScopedCoroContext, ctx.sleep, ctx.time,
 *               ctx.push_to<Quote>, spawn, registerEvent<E>, qb::string<8>, getIndex,
 *               qb::ActorId, qb::Main, addActor<T>
 * @prerequisites 03-coroutines/02-actor-coroutines
 * @expect "[1] one request, one reply, one line: BTC = "
 * @expect "[2] the responder was on another core and the asker did not change: ETH = "
 * @expect "[3] a chain: the middle actor is BOTH asker and responder, with no extra code"
 * @expect "[4] MISSING registerEvent<Quote> on the ASKER: timeout_error after "
 * @expect "[5] a FRESH reply event loses the correlation id: timeout_error after "
 * @expect "[6] resolve_ask returned false: an unsolicited REQUEST, not a reply, symbol "
 * @expect "=== ask complete: no correlation map, no reply handler, no timer ==="
 *
 * WHAT IT REPLACES
 * ----------------
 * Request/response between actors, written by hand, is always the same three pieces: a map
 * from a correlation id to "what I was in the middle of", a reply handler that looks the id up
 * and resumes that work, and a timer that cleans the entry up when no reply ever comes. Four
 * programs in the corpus this one joins hand-rolled exactly that — one so awkwardly that the
 * worker pushed its reply to itself. All three live in the library, and the asker becomes a
 * straight line:
 *
 *     auto q = co_await qb::ask(ctx, oracle, Quote{.symbol = "BTC"}, 500ms);
 *     use(q.response);
 *
 * HOW IT WORKS — because the failures only make sense once you know
 * -----------------------------------------------------------------
 * `qb::Request<Resp>` derives from `qb::AskEvent`, which carries a `correlation_id`. `ask`
 * stamps a fresh id on your request, pushes it to the target with the ASKER as its source, and
 * parks a coroutine keyed on that id. The reply comes back as an **ordinary event to the
 * asker** — there is no private channel — and `resolve_ask(e)` is what hands it to the parked
 * coroutine. One event type round-trips the whole exchange, with the response slot travelling
 * in the payload.
 *
 * Two consequences, and both are below as running code rather than as warnings:
 *   * the asker must `registerEvent<Quote>(*this)` AND call `resolve_ask` — forget either and
 *     every ask times out with nothing else to see (section 4);
 *   * the reply must be the same event, or a copy of it. A freshly constructed one carries
 *     `correlation_id == 0` and resolves nothing (section 5).
 *
 * WHERE THE REST OF THE FAMILY LIVES — deliberately not here
 * ----------------------------------------------------------
 * Asking many actors at once (`qb::ask_all` and its bounded `max_in_flight`, `qb::ask_any`,
 * `qb::ask_quorum`) and bounding a whole CHAIN with one budget (`qb::deadline`, `deadline_in`,
 * `remaining`, `ask_by`) are all built on the single exchange taught here, and they are
 * demonstrated in **`04-patterns/04-scatter-gather`**. One topic, one owner page: this file is
 * the exchange, that one is the fan-out and the budget.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-ask-request-response
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-ask-request-response
 */

#include <chrono>
#include <string_view>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>
#include <qb/string.h>

using namespace std::chrono_literals;

// ONE event type carries the whole conversation. Derive from `qb::Request<Resp>`, add the
// REQUEST fields, and the base supplies the `response` slot plus the correlation id.
// `qb::string<8>` and not `std::string`: events are memcpy-relocated, so a payload may hold no
// pointer into itself (`01-actors/03-event-payloads` measures what happens when one does).
struct Quote : qb::Request<int> {
    qb::string<8> symbol;
    int           from_core{-1}; ///< filled by the responder, so a reply says where it came from
};

// ---------------------------------------------------------------------------------------
// The responder, in its shortest correct form.
//
// `qb::answer(self, e, fn)` does three things in order: `resolve_ask(e)` in case this event is
// a reply to one of OUR OWN asks (returning early if so), then `e.response = fn(e)`, then
// `reply()` of the SAME event — correlation id intact. That is the whole responder.
// ---------------------------------------------------------------------------------------
class Oracle : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }

    void
    on(Quote &q) {
        q.from_core = static_cast<int>(getIndex());
        // `fn` runs inside this handler and MUST NOT throw: a throwing actor handler
        // terminates the worker core, `reply()` is skipped, and the asker would observe only
        // its own timeout. Compute a failure into the payload instead of throwing out of it.
        qb::answer(*this, q, [](Quote const &r) { return std::string_view{r.symbol} == "BTC" ? 64000 : 3400; });
    }
};

// ---------------------------------------------------------------------------------------
// A responder that is ALSO an asker, and needs nothing extra to be both.
// ---------------------------------------------------------------------------------------
class Broker : public qb::Actor {
    qb::ActorId _upstream;

public:
    explicit Broker(qb::ActorId upstream)
        : _upstream(upstream) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }

    void
    on(Quote &q) {
        // A reply to one of OUR upstream asks is not a request and must not be answered.
        // `resolve_ask` already delivered it to the parked coroutine; returning true means
        // "consumed". This is the branch `qb::answer` performs for you in the simple case.
        if (resolve_ask(q))
            return;

        // A real request. We cannot answer yet, so keep a COPY of the request — that copy
        // carries the original asker's correlation id — and fill it in once upstream replies.
        // Copying is exactly right here: `correlation_id` is a payload field, so it survives,
        // while the routing header is rewritten by the push.
        Quote      pending = q;
        const auto who     = q.getSource();
        auto       up      = _upstream;
        spawn([pending, who, up](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            Quote out      = pending;
            auto  upstream = co_await qb::ask(ctx, up, Quote{.symbol = out.symbol}, 500ms);
            out.response   = upstream.response + 5; // the broker's markup
            out.from_core  = upstream.from_core;
            ctx.push_to<Quote>(who, out);
        });
    }
};

// A responder that builds a FRESH reply instead of copying the request. Section 5.
class ForgetfulResponder : public qb::Actor {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Quote>(*this);
        co_return true;
    }

    void
    on(Quote &q) {
        // WRONG, and it looks right: a brand-new `Quote` has `correlation_id == 0`, so the
        // asker's `resolve_ask` finds no pending slot and drops it. The ask then times out
        // exactly as if this actor had never answered at all.
        push<Quote>(q.getSource(), Quote{.symbol = q.symbol, .from_core = 99});
    }
};

// An asker that never registers the exchange type. Section 4. It runs on its own timeline at
// startup; the conductor below simply waits past its 150 ms budget before continuing.
class ForgetfulAsker : public qb::Actor {
    qb::ActorId _oracle;

public:
    explicit ForgetfulAsker(qb::ActorId oracle)
        : _oracle(oracle) {}

    qb::io::async::task<bool>
    onInit() override {
        // registerEvent<Quote>(*this);   <-- THE MISSING LINE, and nothing else is wrong here
        auto oracle = _oracle;
        spawn([oracle](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            const auto t0 = ctx.time();
            try {
                (void) co_await qb::ask(ctx, oracle, Quote{.symbol = "BTC"}, 150ms);
                qb::io::cout() << "    UNREACHABLE: an unregistered asker got its reply\n";
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[4] MISSING registerEvent<Quote> on the ASKER: timeout_error after " << (ctx.time() - t0) / 1'000'000
                               << " ms. The Oracle DID answer; the reply arrived at this actor and was "
                                  "dropped, because nothing routed it to resolve_ask\n";
            }
        });
        co_return true;
    }
};

// ---------------------------------------------------------------------------------------
// The asker, and the conductor of the whole demonstration.
// ---------------------------------------------------------------------------------------
class Conductor : public qb::Actor {
    qb::ActorId _oracle;
    qb::ActorId _remote;
    qb::ActorId _broker;
    qb::ActorId _fresh;

public:
    Conductor(qb::ActorId oracle, qb::ActorId remote, qb::ActorId broker, qb::ActorId fresh)
        : _oracle(oracle)
        , _remote(remote)
        , _broker(broker)
        , _fresh(fresh) {}

    qb::io::async::task<bool>
    onInit() override {
        // THE line. Without it every `ask` below times out and nothing says why.
        registerEvent<Quote>(*this);

        auto oracle = _oracle, remote = _remote, broker = _broker, fresh = _fresh;
        spawn([oracle, remote, broker, fresh](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // ---- 1. the exchange -------------------------------------------------------
            auto one = co_await qb::ask(ctx, oracle, Quote{.symbol = "BTC"}, 500ms);
            qb::io::cout() << "[1] one request, one reply, one line: BTC = " << one.response << " (from core " << one.from_core << ")\n";

            // ---- 2. the responder is on another core -----------------------------------
            // Nothing in the asker changed. The request crosses the core boundary and the
            // reply crosses back; `correlation_id` is a payload field, so it survives both
            // memcpy relocations.
            auto two = co_await qb::ask(ctx, remote, Quote{.symbol = "ETH"}, 500ms);
            qb::io::cout() << "[2] the responder was on another core and the asker did not change: ETH = " << two.response << " (from core "
                           << two.from_core << ")\n";

            // ---- 3. a chain ------------------------------------------------------------
            auto three = co_await qb::ask(ctx, broker, Quote{.symbol = "BTC"}, 800ms);
            qb::io::cout() << "[3] a chain: the middle actor is BOTH asker and responder, with no extra code "
                              "for either — BTC through the broker = "
                           << three.response << "\n";

            // ---- 4. the missing registerEvent, already running --------------------------
            // ForgetfulAsker started its own 150 ms ask at engine start; wait past it so its
            // finding is printed in order rather than interleaved with ours.
            co_await ctx.sleep(250ms);

            // ---- 5. the fresh-reply mistake, run ----------------------------------------
            const auto t0 = ctx.time();
            try {
                (void) co_await qb::ask(ctx, fresh, Quote{.symbol = "BTC"}, 150ms);
                qb::io::cout() << "    UNREACHABLE: a correlation-less reply resolved an ask\n";
            } catch (qb::io::async::timeout_error const &) {
                qb::io::cout() << "[5] a FRESH reply event loses the correlation id: timeout_error after " << (ctx.time() - t0) / 1'000'000
                               << " ms. Copy the request (`Quote r = q;`) or use qb::answer, which "
                                  "replies the same event\n";
            }

            // ---- 6. resolve_ask says "not mine" ----------------------------------------
            // An unsolicited Quote, sent to ourselves with no ask behind it. `resolve_ask`
            // returns false and the handler takes its other branch — which is what lets ONE
            // handler serve both roles.
            ctx.push_to<Quote>(ctx.id(), Quote{.symbol = "XMR"});
            co_await ctx.sleep(50ms);

            qb::io::cout() << "=== ask complete: no correlation map, no reply handler, no timer ===\n";
            qb::Main::stop();
        });
        co_return true;
    }

    void
    on(Quote &q) {
        if (resolve_ask(q))
            return; // a reply to one of our asks — already delivered to the parked coroutine
        // NOTE WHAT ELSE ARRIVES HERE. This line prints TWICE: once for the deliberate XMR
        // request of section 6, and once for the BTC event ForgetfulResponder built fresh in
        // section 5. The correlation-less "reply" is not recognised as a reply at all — it
        // falls through to the unsolicited branch, and its ask times out regardless. That is
        // precisely what the mistake looks like from the asker's side.
        qb::io::cout() << "[6] resolve_ask returned false: an unsolicited REQUEST, not a reply, symbol " << q.symbol
                       << " — a correlation-less REPLY lands in this same branch\n";
    }
};

int
main() {
    qb::Main engine;

    auto oracle = engine.addActor<Oracle>(0);
    auto remote = engine.addActor<Oracle>(1); // same class, other core
    auto broker = engine.addActor<Broker>(0, oracle);
    auto fresh  = engine.addActor<ForgetfulResponder>(0);

    engine.addActor<ForgetfulAsker>(0, oracle);
    engine.addActor<Conductor>(0, oracle, remote, broker, fresh);

    qb::io::cout() << "=== qb::ask: one request, one reply, one line ===\n\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
