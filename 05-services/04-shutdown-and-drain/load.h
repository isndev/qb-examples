/**
 * @file examples/05-services/04-shutdown-and-drain/load.h
 * @brief The load the service is shut down UNDER, and the counters that judge the result.
 *
 * @details
 * A shutdown demonstration with no traffic proves nothing: every server drains an empty queue
 * correctly. So the program brings its own clients — ordinary actors on their own core — and its
 * own trigger, and raises SIGTERM at the one moment where the work is genuinely in flight.
 *
 * The counters are `inline` variables (one definition, no `extern` and no .cpp entry) and atomic
 * because they are written on the client core and read by `main()` after `join()`.
 */

#pragma once

#include <atomic>
#include <vector>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include "events.h"

namespace drain_demo {

inline std::atomic<int> g_requests_sent{0};
inline std::atomic<int> g_replies_seen{0};
inline std::atomic<int> g_work_completed{0};
inline std::atomic<int> g_work_abandoned{0};

/// A client actor: waits to be told where the service is, connects, asks once, waits for its answer.
class Client
    : public qb::Actor
    , public qb::io::use<Client>::tcp::client<> {
    const int _n;
    bool      _awaiting = false;

public:
    using Protocol = qb::protocol::text::command<Client>;

    explicit Client(int n)
        : _n(n) {}

    qb::io::async::task<bool> onInit() override;

    void on(Connect &evt);
    void on(Protocol::message &&msg);
    void on(qb::SignalEvent const &e);
    void on(qb::io::async::event::disconnected const &);
};

/// Starts the load once the service is really up, then raises SIGTERM while the work is in flight.
class Conductor : public qb::Actor {
    const std::vector<qb::ActorId> _clients;

public:
    explicit Conductor(std::vector<qb::ActorId> clients)
        : _clients(std::move(clients)) {}

    qb::io::async::task<bool> onInit() override;

    void on(ServiceUp &evt);
};

} // namespace drain_demo
