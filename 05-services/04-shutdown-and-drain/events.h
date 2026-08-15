/**
 * @file examples/05-services/04-shutdown-and-drain/events.h
 * @brief The events this service uses to move a connection, a unit of work and a shutdown
 *        decision between the three actors that own them.
 *
 * @details
 * Every field below obeys the cross-core payload contract taught in `01-actors/03-event-payloads`:
 * an event that crosses a core boundary is memcpy-RELOCATED and its source destructor never runs,
 * so nothing here may hold a pointer into itself. `qb::io::tcp::socket` is a movable handle around
 * an int, `qb::uuid` is 16 bytes, and the two strings are `qb::string<N>` — fixed inline storage,
 * never a heap pointer. A `std::string` here would be correct on macOS and corrupt memory on Linux,
 * which is exactly the bug the contract exists to prevent.
 *
 * Nothing declares a member called `id`, `dest`, `source`, `bucket_size` or `state`: those five
 * names belong to `qb::Event`'s own routing header, and shadowing one is a compile error naming the
 * field and the type.
 */

#pragma once

#include <cstdint>
#include <qb/event.h>
#include <qb/io/tcp/socket.h>
#include <qb/string.h>
#include <qb/uuid.h>

namespace drain_demo {

/// A freshly accepted connection, on its way from the acceptor's core to the pool's.
struct NewConnection : qb::Event {
    qb::io::tcp::socket socket;
};

/// The acceptor bound a port and is listening. Sent once, to the conductor.
struct ServiceUp : qb::Event {
    std::uint16_t port{0};
};

/// The conductor telling a client actor to go, and where. A client never guesses the port and
/// never sleeps waiting for one: it is TOLD, which is what makes the startup order deterministic
/// without a single `sleep_for` anywhere in this program.
struct Connect : qb::Event {
    std::uint16_t port{0};
};

/// One unit of work a session asked for, on its way to the pool's coroutine.
struct WorkRequest : qb::Event {
    qb::uuid       session;
    qb::string<32> what;
};

/// The same unit, finished, on its way back to the actor that owns the session.
struct WorkDone : qb::Event {
    qb::uuid       session;
    qb::string<64> answer;
};

// There is deliberately NO `BeginDrain` event here, and its absence is the lesson. The first
// draft had one: the acceptor caught SIGTERM and told the pool to start draining. That is a
// central shutdown coordinator, and this framework does not need one — the signal is BROADCAST to
// every actor as a `qb::SignalEvent`, on each actor's own thread, so each one already knows.
// The acceptor closes its listener, the pool starts draining, the clients wait for their answers.
// Three independent decisions, no ordering to get wrong, no event to invent.

} // namespace drain_demo
