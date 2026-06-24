/**
 * @file actors/tcp_listener.h
 * @brief TcpListener – TCP acceptor with round-robin dispatch to TaskManagers.
 *
 * ## Design
 * TcpListener sits on its own VirtualCore (core 0).  It owns the listening
 * socket and accepts every incoming TCP connection.  Each accepted socket is
 * immediately moved into a `NewConnectionEvent` and pushed to the next
 * TaskManager in a round-robin sequence.  The TaskManager then owns the fd
 * and wraps it in an HTTP session.
 *
 * This separation means the hot accept loop is never blocked by HTTP
 * processing, and load is spread evenly across the TaskManager pool.
 *
 * ## QB patterns used
 * - `qb::Actor`                        – event loop integration
 * - `qb::io::use<T>::tcp::acceptor`    – non-blocking accept via libev
 * - `push<NewConnectionEvent>`         – cross-core socket transfer
 * - `broadcast<qb::KillEvent>`         – orderly shutdown on listen failure
 */
#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/uri.h>
#include <vector>
#include "events.h"

namespace taskmanager {
namespace actors {

class TcpListener : public qb::Actor
                  , public qb::io::use<TcpListener>::tcp::acceptor {
public:
    /**
     * @param uri           Listening address, e.g. `tcp://0.0.0.0:8080`.
     * @param task_managers Ordered list of TaskManager actor IDs for round-robin.
     */
    TcpListener(qb::io::uri uri, std::vector<qb::ActorId> task_managers)
        : _uri(std::move(uri))
        , _targets(std::move(task_managers)) {}

    qb::io::async::task<bool> onInit() override {
        if (_targets.empty()) {
            qb::io::cerr() << "[TcpListener] no TaskManagers configured\n";
            co_return false;
        }
        if (!listen(_uri)) {
            qb::io::cerr() << "[TcpListener] failed to listen on " << _uri.source() << '\n';
            co_return false;
        }
        qb::io::cout() << "[TcpListener] listening on " << _uri.source()
                       << "  (" << _targets.size() << " workers)\n";
        start();
        co_return true;
    }

    /** Called by qb-io for every accepted TCP connection. */
    void on(accepted_socket_type &&sock) {
        auto &evt  = push<NewConnectionEvent>(_targets[_rr++ % _targets.size()]);
        evt.socket = std::move(sock);
    }

    /** Acceptor lost its socket – propagate shutdown to the whole engine. */
    void on(qb::io::async::event::disconnected const &) {
        qb::io::cerr() << "[TcpListener] acceptor disconnected – shutting down\n";
        broadcast<qb::KillEvent>();
    }

private:
    qb::io::uri              _uri;
    std::vector<qb::ActorId> _targets;
    std::size_t              _rr{0}; ///< Round-robin cursor.
};

} // namespace actors
} // namespace taskmanager
