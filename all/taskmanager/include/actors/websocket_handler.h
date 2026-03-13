/**
 * @file actors/websocket_handler.h
 * @brief WebSocketHandler – WebSocket session pool + Redis Pub/Sub subscriber.
 *
 * ## Responsibility
 * WebSocketHandler is **not** a QB Actor; it is an inner component owned by
 * TaskManager and runs on the same VirtualCore.  It has two roles:
 *
 * 1. **Session pool** (`io_handler<WebSocketHandler, WsSession>`):
 *    Manages the lifetime of every WsSession.  Sessions are registered via
 *    `upgrade_connection()` and automatically removed on disconnect.
 *
 * 2. **Redis subscriber** (`redis::tcp::consumer<WebSocketHandler>`):
 *    Maintains a long-lived subscription to the `tasks:events` Redis channel.
 *    Every message received is deserialised and broadcast to all WS clients.
 *
 * ## Data flow
 * ```
 * Redis PUBLISH tasks:events  ──►  on(redis::message&&)
 *                                         │
 *                                  broadcast_to_all(json)
 *                                         │
 *                              ┌──────────┴──────────┐
 *                         WsSession 1          WsSession N
 *                       send_json(data)      send_json(data)
 * ```
 *
 * ## HTTP → WebSocket upgrade sequence
 * ```
 * TaskManager::handle_ws_upgrade
 *   └─ extractSession(http_session_id)     ← steals the TCP socket
 *       └─ upgrade_connection(sock, req, resp)
 *           ├─ registerSession(sock)        ← creates WsSession
 *           └─ switch_protocol<ws>()       ← performs handshake → 101 response
 * ```
 */
#pragma once

#include <redis/redis.h>
#include <http/http.h>
#include <ws/ws.h>
#include <qb/json.h>
#include "ws_session.h"

namespace taskmanager {
namespace actors {

class TaskManager; // back-reference for access to TaskManager state if needed

/**
 * @brief Inner component that owns all WS sessions and the Redis SUB connection.
 *
 * Constructed by TaskManager; its lifetime is tied to the owning actor.
 * `init()` **must** be called from `TaskManager::onInit()` before any WebSocket
 * connections are accepted.
 */
class WebSocketHandler
    : public qb::io::use<WebSocketHandler>::tcp::io_handler<WsSession>
    , public qb::redis::tcp::consumer<WebSocketHandler> {
public:
    /**
     * @param manager    Back-reference to the owning TaskManager.
     * @param redis_uri  Redis URI for the subscriber connection.
     */
    WebSocketHandler(TaskManager &manager, qb::io::uri redis_uri);

    /**
     * @brief Connect to Redis and subscribe to `tasks:events`.
     * @return false if the Redis connection or subscription fails.
     */
    bool init();

    // ── qb-io / qb-redis callbacks ───────────────────────────────────────────

    /** Called by io_handler when a new WsSession becomes active. */
    void on(WsSession &session);

    /** Called by the Redis consumer when a Pub/Sub message arrives. */
    void on(qb::redis::message &&msg);

    // ── Public API ───────────────────────────────────────────────────────────

    /**
     * @brief Perform the HTTP → WebSocket protocol upgrade.
     *
     * Registers a new WsSession from the extracted TCP socket, performs the
     * WebSocket handshake, and writes the 101 response directly on the socket.
     *
     * @param sock     Extracted TCP socket (ownership transferred).
     * @param request  Original HTTP upgrade request.
     * @param response HTTP response object to fill (101 Switching Protocols).
     * @return true on success; false if the handshake fails (session is
     *         immediately disconnected in that case).
     */
    bool upgrade_connection(qb::io::tcp::socket     &&sock,
                            const qb::http::Request  &request,
                            qb::http::Response       &response);

    /**
     * @brief Send @p data as a JSON text frame to **every** connected WS client.
     * @param data  Serialisable JSON value.
     */
    void broadcast_to_all(const qb::json &data);

    /** Number of currently connected WebSocket clients. */
    [[nodiscard]] std::size_t client_count() const noexcept {
        return session_count();
    }

private:
    TaskManager  &_manager;
    qb::io::uri   _redis_uri;
};

} // namespace actors
} // namespace taskmanager
