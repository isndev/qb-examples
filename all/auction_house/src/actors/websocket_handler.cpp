/**
 * @file src/actors/websocket_handler.cpp
 * @brief WebSocketHandler (coroutine Redis Pub/Sub) + WsSession implementations.
 */

#include "auction_house/actors/websocket_handler.h"
#include <qb/io.h>
#include "auction_house/actors/auction_manager.h"

namespace auction_house {
namespace actors {

// ─── WsSession ────────────────────────────────────────────────────────────────

void
WsSession::on(ws_protocol::message &&msg) {
    try {
        auto json = qb::json::parse(msg.ws.data().view());
        qb::io::cout() << "[WsSession " << id() << "] Received: " << json.value("type", "unknown") << "\n";
        server().handle_ws_message(json, *this); // route to the owning handler
    } catch (const std::exception &e) {
        qb::io::cerr() << "[WsSession " << id() << "] Parse error: " << e.what() << "\n";
    }
}

void
WsSession::send_json(const qb::json &msg) {
    qb::http::ws::MessageText text;
    text << msg.dump();
    *this << text;
}

// ─── WebSocketHandler ───────────────────────────────────────────────────────

WebSocketHandler::WebSocketHandler(AuctionManager &manager, qb::io::uri redis_uri)
    : _manager(manager)
    , _redis_uri(std::move(redis_uri))
    , _sub(_redis_uri) {}

qb::io::async::task<bool>
WebSocketHandler::connect_subscriber() {
    if (!co_await _sub.connect()) {
        qb::io::cerr() << "[WebSocketHandler] Redis SUB connect failed\n";
        co_return false;
    }
    auto sub = co_await _sub.subscribe(std::string{"auction:events"});
    if (!sub.ok()) {
        qb::io::cerr() << "[WebSocketHandler] subscribe failed: " << sub.error() << "\n";
        co_return false;
    }
    qb::io::cout() << "[WebSocketHandler] subscribed to auction:events\n";
    co_return true;
}

qb::io::async::task<void>
WebSocketHandler::consume_loop() {
    while (auto msg = co_await _sub.receive()) {
        try {
            auto data = qb::json::parse(msg->payload);
            qb::io::cout() << "[WebSocketHandler] broadcasting " << data.value("type", "?") << " to " << client_count() << " clients\n";
            broadcast_to_all(data);
        } catch (const std::exception &e) {
            qb::io::cerr() << "[WebSocketHandler] bad Redis message: " << e.what() << "\n";
        }
    }
    qb::io::cout() << "[WebSocketHandler] consume loop ended\n";
}

void
WebSocketHandler::shutdown() {
    _sub.disconnect();
}

void
WebSocketHandler::on(WsSession &session) {
    qb::io::cout() << "[WebSocketHandler] WS session active: " << session.id() << "  total=" << client_count() << "\n";
    // Do NOT send here: on() fires during the upgrade, before the 101 is flushed.
    // The client sends a ping/subscribe after connecting.
}

bool
WebSocketHandler::upgrade_connection(qb::io::tcp::socket &&sock, const qb::http::Request &request, qb::http::Response &response) {
    auto *ws_session = registerSession(std::move(sock));
    if (!ws_session) {
        qb::io::cerr() << "[WebSocketHandler] upgrade failed (session limit reached)\n";
        return false;
    }
    if (ws_session->switch_protocol<WsSession::ws_protocol>(*ws_session, request, response)) {
        *ws_session << response; // 101 Switching Protocols
        return true;
    }
    ws_session->disconnect();
    return false;
}

void
WebSocketHandler::broadcast_to_all(const qb::json &data) {
    qb::http::ws::MessageText text;
    text << data.dump();
    this->stream(text); // unambiguous: stream() comes only from the io_handler base now
}

void
WebSocketHandler::handle_ws_message(const qb::json &data, WsSession &session) {
    const std::string type = data.value("type", "");
    if (type == "ping") {
        session.send_json({{"type", "pong"}});
    } else if (type == "subscribe_lot") {
        session.send_json({{"type", "subscribed"}, {"lot_id", data.value("lot_id", 0)}});
    }
    // Mutations go through the HTTP API; WS is mainly server→client broadcast.
}

} // namespace actors
} // namespace auction_house
