/**
 * @file examples/05-services/04-shutdown-and-drain/load.cpp
 * @brief The client actors and the conductor that starts them — implementation.
 *
 * @details
 * Two things here are worth more than the traffic they generate.
 *
 * **Nothing sleeps waiting for the service.** The acceptor binds an EPHEMERAL port, so the port is
 * not known until it is bound; a client that guessed, or that slept "long enough", would be a race
 * dressed as a demo. Instead the acceptor tells the conductor which port it got, and the conductor
 * tells the clients. Every actor exists before `start()`, so every id is valid — and an event sent
 * to an actor whose own `onInit()` has not finished yet is STASHED and replayed when it activates.
 *
 * **A client with a request outstanding does not die on SIGTERM either.** The claim this program
 * makes is that an accepted request is ANSWERED; an answer that nobody is left to receive proves
 * the opposite. So `Client` re-registers `qb::SignalEvent` too, and leaves only when it is idle.
 */

#include <csignal>
#include <string>
#include <qb/io.h>
#include <qb/main.h>
#include "load.h"

using namespace std::chrono_literals;

namespace drain_demo {

qb::io::async::task<bool>
Client::onInit() {
    registerEvent<Connect>(*this);
    // THE line for this actor. Without it the base handler runs and this client dies the moment
    // SIGTERM is broadcast — with its request still in flight and nobody left to receive the reply.
    registerEvent<qb::SignalEvent>(*this);
    co_return true;
}

void
Client::on(Connect &evt) {
    if (transport().connect_v4("127.0.0.1", evt.port) != qb::io::SocketStatus::Done) {
        qb::io::cerr() << "[client " << _n << "] could not connect\n";
        kill();
        return;
    }
    start();
    *this << "unit-" << std::to_string(_n) << Protocol::end;
    _awaiting = true;
    g_requests_sent.fetch_add(1, std::memory_order_relaxed);
}

void
Client::on(Protocol::message &&msg) {
    _awaiting = false;
    g_replies_seen.fetch_add(1, std::memory_order_relaxed);
    qb::io::cout() << "[client " << _n << "] " << msg.text << "\n";
    kill();
}

// `const &`, and NOT `override`: the base declaration is not virtual, and this handler runs only
// because `registerEvent<qb::SignalEvent>` re-pointed the subscription at this type.
void
Client::on(qb::SignalEvent const &e) {
    if (e.signum != SIGINT && e.signum != SIGTERM)
        return;
    if (!_awaiting)
        kill(); // nothing outstanding: leaving now is the polite thing to do
}

void
Client::on(qb::io::async::event::disconnected const &) {
    kill();
}

qb::io::async::task<bool>
Conductor::onInit() {
    registerEvent<ServiceUp>(*this);
    co_return true;
}

void
Conductor::on(ServiceUp &evt) {
    for (auto client : _clients)
        push<Connect>(client).port = evt.port;

    spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
        // Late enough that every client has connected and asked, early enough that none of the
        // 300 ms units has finished. That window is the entire subject of this program.
        co_await ctx.sleep(150ms);
        qb::io::cout() << "\n[conductor] raising SIGTERM with " << g_requests_sent.load() << " request(s) in flight\n";
        std::raise(SIGTERM);
    });
}

} // namespace drain_demo
