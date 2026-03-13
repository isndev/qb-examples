/**
 * @file actors/ws_session.h
 * @brief WsSession – one long-lived WebSocket session per upgraded connection.
 *
 * ## Lifecycle
 * 1. An HTTP connection arrives at TaskManager.
 * 2. The client sends `GET /ws` with an `Upgrade: websocket` header.
 * 3. `TaskManager::handle_ws_upgrade` extracts the TCP socket and calls
 *    `WebSocketHandler::upgrade_connection`, which calls `registerSession`
 *    to create this WsSession and performs the WS handshake.
 * 4. From this point, the session is owned by WebSocketHandler and receives
 *    frames via `on(ws_protocol::message&&)`.
 * 5. The session is destroyed when the client disconnects.
 *
 * ## QB convention
 * Like HttpSession, this is a thin CRTP wrapper.  Business logic lives in
 * WebSocketHandler.  Only inbound frame handling (`on`) and the send helper
 * (`send_json`) belong here.
 */
#pragma once

#include <ws/ws.h>
#include <qb/json.h>

namespace taskmanager {
namespace actors {

// Forward-declare the owning handler to break the circular dependency.
class WebSocketHandler;

/**
 * @brief WebSocket session bound to a WebSocketHandler.
 *
 * Uses the qb-websocket protocol adapter.  Inbound frames are delivered
 * through `on(ws_protocol::message&&)`.  Outbound data is sent via
 * `send_json()` (called by WebSocketHandler when broadcasting events).
 */
class WsSession : public qb::io::use<WsSession>::tcp::client<WebSocketHandler> {
public:
    using ws_protocol = qb::http::ws::protocol<WsSession>;

    explicit WsSession(WebSocketHandler &handler) : client(handler) {}

    /**
     * @brief Handle an inbound WebSocket frame.
     *
     * Parses the payload as JSON and sends an `{"type":"ack"}` reply.
     * Malformed frames are silently discarded.
     */
    void on(ws_protocol::message &&msg);

    /**
     * @brief Serialise @p data to JSON and push it as a WebSocket text frame.
     * @param data  Any JSON-serialisable value.
     */
    void send_json(const qb::json &data);
};

} // namespace actors
} // namespace taskmanager
