/**
 * @file src/actors/websocket_handler.cpp
 * @brief WebSocketHandler + WsSession implementations.
 */

#include "auction_house/actors/websocket_handler.h"
#include "auction_house/actors/auction_manager.h"
#include <qb/io.h>

namespace auction_house {
namespace actors {

// ─── WsSession ────────────────────────────────────────────────────────────────

void WsSession::on(ws_protocol::message &&msg) {
    try {
        // CRITICAL: Use msg.ws.data().view() to access payload
        auto json = qb::json::parse(msg.ws.data().view());

        qb::io::cout() << "[WsSession " << id() << "] Received: "
                      << json.value("type", "unknown") << "\n";

        // Route to handler
        server().handle_ws_message(json, *this);

    } catch (const std::exception &e) {
        qb::io::cerr() << "[WsSession " << id() << "] Parse error: "
                      << e.what() << "\n";
    }
}

void WsSession::send_json(const qb::json &msg) {
    qb::http::ws::MessageText text;
    text << msg.dump();
    *this << text;
}

// ─── WebSocketHandler ───────────────────────────────────────────────────────

WebSocketHandler::WebSocketHandler(AuctionManager &manager,
                                     qb::io::uri redis_uri)
    : _manager(manager)
    , _redis_uri(std::move(redis_uri)) {}

bool WebSocketHandler::init() {
    if (!connect(_redis_uri)) {
        qb::io::cerr() << "[WebSocketHandler] Redis connect failed\n";
        return false;
    }

    subscribe(std::string{"auction:events"});
    start();

    qb::io::cout() << "[WebSocketHandler] Subscribed to auction:events\n";
    return true;
}

void WebSocketHandler::on(WsSession &session) {
    qb::io::cout() << "[WebSocketHandler] WS session active: " << session.id()
                  << "  total=" << client_count() << '\n';
    // NOTE: Do NOT send messages here - on() is called during upgrade
    // before the 101 Switching Protocols response is fully sent.
    // Client should send a ping/subscribe message after connecting.
}

void WebSocketHandler::on(qb::redis::message &&msg) {
    try {
        auto data = qb::json::parse(std::string{msg.message});

        qb::io::cout() << "[WebSocketHandler] Broadcasting: "
                      << data.value("type", "?")
                      << " to " << client_count() << " clients\n";

        broadcast_to_all(data);

    } catch (const std::exception &e) {
        qb::io::cerr() << "[WebSocketHandler] Bad Redis message: "
                      << e.what() << "\n";
    }
}

bool WebSocketHandler::upgrade_connection(qb::io::tcp::socket &&sock,
                                            const qb::http::Request &request,
                                            qb::http::Response &response) {
    auto &ws_session = registerSession(std::move(sock));

    if (ws_session.switch_protocol<WsSession::ws_protocol>(
            ws_session, request, response)) {
        // Send 101 Switching Protocols
        ws_session << response;
        return true;
    }

    ws_session.disconnect();
    return false;
}

void WebSocketHandler::broadcast_to_all(const qb::json &data) {
    qb::http::ws::MessageText text;
    text << data.dump();

    // CRITICAL: Fully qualify to resolve ambiguity from multiple inheritance
    qb::io::async::io_handler<WebSocketHandler, WsSession>::stream(text);
}

// Handle messages from WebSocket clients
void WebSocketHandler::handle_ws_message(const qb::json &data, WsSession &session) {
    std::string type = data.value("type", "");

    if (type == "ping") {
        session.send_json({{"type", "pong"}});
    }
    else if (type == "subscribe_lot") {
        int lot_id = data.value("lot_id", 0);
        session.send_json({
            {"type", "subscribed"},
            {"lot_id", lot_id}
        });
    }
    // Client messages are primarily handled via HTTP API
    // WS is mainly for server-to-client broadcast
}

} // namespace actors
} // namespace auction_house
