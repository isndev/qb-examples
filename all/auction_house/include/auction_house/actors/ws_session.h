/**
 * @file actors/ws_session.h
 * @brief WsSession - WebSocket session CRTP wrapper.
 *
 * Handles inbound WebSocket frames and provides send helper.
 */
#pragma once

#include <http/ws.h>
#include <qb/json.h>

namespace auction_house {
namespace actors {

// Forward declaration
class WebSocketHandler;

/**
 * @brief WebSocket session bound to WebSocketHandler.
 *
 * CRTP Pattern: qb::io::use<Derived>::tcp::client<Parent>
 * Plus: qb::http::ws::protocol<Derived> for WebSocket protocol.
 */
class WsSession : public qb::io::use<WsSession>::tcp::client<WebSocketHandler> {
public:
    using ws_protocol = qb::http::ws::protocol<WsSession>;

    explicit WsSession(WebSocketHandler &handler)
        : client(handler) {}

    /**
     * @brief Handle inbound WebSocket frame.
     *
     * Parses JSON payload and routes to handler.
     */
    void on(ws_protocol::message &&msg);

    /**
     * @brief Send JSON as WebSocket text frame.
     */
    void send_json(const qb::json &data);
};

} // namespace actors
} // namespace auction_house
