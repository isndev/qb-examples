/**
 * @file actors/websocket_handler.h
 * @brief WebSocketHandler – WebSocket session pool + coroutine Redis Pub/Sub.
 *
 * ## Responsibility
 * WebSocketHandler is **not** a QB Actor; it is an inner component owned by
 * TaskManager and runs on the same VirtualCore. Two roles:
 *
 * 1. **Session pool** (`io_handler<WsSession>`): owns every WsSession; sessions
 *    are registered via `upgrade_connection()` and removed on disconnect.
 *
 * 2. **Coroutine Redis subscriber** (`qb::redis::tcp::co_consumer`): a long-lived
 *    subscription to `tasks:events`. Instead of a callback `on(message&&)`, the
 *    owner drives a coroutine loop — `while (auto m = co_await receive()) …` —
 *    and broadcasts each payload to all WS clients.
 *
 * ## Data flow
 * ```
 * Redis PUBLISH tasks:events ─► co_await consume_loop() ─► broadcast_to_all(json)
 *                                                         ├─ WsSession 1 … N
 * ```
 *
 * ## Lifecycle
 * `connect_subscriber()` is `co_await`ed from `TaskManager::onInit()`; the owner
 * then spawns `consume_loop()` as an actor-scoped coroutine. `shutdown()` closes
 * the subscription so the loop ends (its `receive()` yields `std::nullopt`).
 */
#pragma once

#include <qbm/http/http.h>
#include <qbm/http/ws.h>
#include <redis/redis.h>
#include <qb/io/async.h>
#include <qb/json.h>
#include "ws_session.h"

namespace taskmanager {
namespace actors {

class TaskManager; // back-reference (state access if needed)

/**
 * @brief Inner component: owns all WS sessions and the coroutine Redis SUB.
 *
 * Constructed by TaskManager; lifetime tied to the owning actor.
 */
class WebSocketHandler : public qb::io::use<WebSocketHandler>::tcp::io_handler<WsSession> {
public:
    /**
     * @param manager    Back-reference to the owning TaskManager.
     * @param redis_uri  Redis URI for the subscriber connection.
     */
    WebSocketHandler(TaskManager &manager, qb::io::uri redis_uri);

    // ── Coroutine lifecycle ──────────────────────────────────────────────────

    /** Connect the subscriber and subscribe to `tasks:events`. */
    qb::io::async::task<bool> connect_subscriber();

    /**
     * @brief Drain published messages forever, broadcasting each to WS clients.
     * @details Runs until the subscription closes (`shutdown()` / disconnect),
     *          at which point `receive()` yields `std::nullopt` and the loop ends.
     */
    qb::io::async::task<void> consume_loop();

    /** Close the subscriber so `consume_loop()` returns (idempotent). */
    void shutdown();

    // ── qb-io callback ───────────────────────────────────────────────────────

    /** Called by io_handler when a new WsSession becomes active (logging). */
    void on(WsSession &session);

    // ── Public API ───────────────────────────────────────────────────────────

    /**
     * @brief Perform the HTTP → WebSocket protocol upgrade on @p sock.
     * @return true on success; on a bad handshake the session is disconnected.
     */
    bool upgrade_connection(qb::io::tcp::socket &&sock, const qb::http::Request &request, qb::http::Response &response);

    /** Send @p data as a JSON text frame to every connected WS client. */
    void broadcast_to_all(const qb::json &data);

    /** Number of currently connected WebSocket clients. */
    [[nodiscard]] std::size_t
    client_count() const noexcept {
        return session_count();
    }

private:
    TaskManager                &_manager;
    qb::io::uri                 _redis_uri;
    qb::redis::tcp::co_consumer _sub; ///< coroutine Pub/Sub subscriber
};

} // namespace actors
} // namespace taskmanager
