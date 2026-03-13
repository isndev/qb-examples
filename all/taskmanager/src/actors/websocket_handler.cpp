/**
 * @file src/actors/websocket_handler.cpp
 * @brief WebSocketHandler + WsSession implementations.
 *
 * Split from task_manager.cpp to keep translation units focused:
 * - WsSession  : inbound frame handling, JSON send helper.
 * - WebSocketHandler : Redis subscriber, session pool, WS upgrade.
 */

#include "actors/websocket_handler.h"
#include "actors/task_manager.h"
#include <qb/io.h>

namespace taskmanager {
namespace actors {

// ─── WsSession ────────────────────────────────────────────────────────────────

/**
 * Parses the inbound WebSocket text frame as JSON and replies with an ACK.
 * Malformed payloads are silently discarded (no way to signal a protocol
 * error over WS without closing the connection).
 */
void WsSession::on(ws_protocol::message &&msg) {
    try {
        auto json = qb::json::parse(msg.ws.data().view());
        qb::io::cout() << "[WsSession " << id() << "] rx type="
                       << json.value("type", "unknown") << '\n';

        send_json({
            {"type",      "ack"},
            {"received",  json.value("type", "unknown")},
            {"timestamp", qb::Timestamp::now().count()}
        });
    } catch (const std::exception &e) {
        qb::io::cerr() << "[WsSession " << id() << "] parse error: "
                       << e.what() << '\n';
    }
}

/**
 * Serialises @p msg to a JSON string and sends it as a WebSocket text frame.
 */
void WsSession::send_json(const qb::json &msg) {
    qb::http::ws::MessageText text;
    text << msg.dump();
    *this << text;
}

// ─── WebSocketHandler ─────────────────────────────────────────────────────────

WebSocketHandler::WebSocketHandler(TaskManager &manager, qb::io::uri redis_uri)
    : _manager(manager)
    , _redis_uri(std::move(redis_uri)) {}

/**
 * Connects the Redis subscriber and subscribes to `tasks:events`.
 * The CRTP consumer will call `on(redis::message&&)` for every matching
 * message published to that channel.
 */
bool WebSocketHandler::init() {
    if (!connect(_redis_uri)) {
        qb::io::cerr() << "[WebSocketHandler] Redis sub connect failed\n";
        return false;
    }
    subscribe(std::string{"tasks:events"});
    start();
    qb::io::cout() << "[WebSocketHandler] Subscribed to tasks:events\n";
    return true;
}

/**
 * Called by io_handler whenever a new WsSession becomes fully active
 * (after the protocol switch completes).  Used for logging / bookkeeping.
 */
void WebSocketHandler::on(WsSession &session) {
    qb::io::cout() << "[WebSocketHandler] WS session active: " << session.id()
                   << "  total=" << client_count() << '\n';
}

/**
 * Called by the Redis CRTP consumer on every Pub/Sub message received on
 * `tasks:events`.  Parses the JSON payload and broadcasts it to all WS clients.
 */
void WebSocketHandler::on(qb::redis::message &&msg) {
    try {
        auto data = qb::json::parse(std::string{msg.message});
        qb::io::cout() << "[WebSocketHandler] broadcast action="
                       << data.value("action", "?")
                       << "  clients=" << client_count() << '\n';
        broadcast_to_all(data);
    } catch (const std::exception &e) {
        qb::io::cerr() << "[WebSocketHandler] malformed Redis payload: "
                       << e.what() << '\n';
    }
}

/**
 * Performs the HTTP → WebSocket protocol upgrade on the extracted socket.
 *
 * Steps:
 * 1. Register the raw TCP socket as a new WsSession (io_handler takes
 *    ownership).
 * 2. Call `switch_protocol<ws_protocol>` which runs the WS handshake and
 *    populates `response` with the 101 headers.
 * 3. Write the 101 response directly on the socket so the client can start
 *    sending frames.
 *
 * On failure the newly registered session is disconnected immediately to
 * release the file descriptor.
 */
bool WebSocketHandler::upgrade_connection(qb::io::tcp::socket    &&sock,
                                          const qb::http::Request &request,
                                          qb::http::Response      &response) {
    auto &ws_session = registerSession(std::move(sock));

    if (ws_session.switch_protocol<WsSession::ws_protocol>(
            ws_session, request, response)) {
        ws_session << response;
        qb::io::cout() << "[WebSocketHandler] upgrade OK: " << ws_session.id() << '\n';
        return true;
    }

    qb::io::cerr() << "[WebSocketHandler] upgrade failed (bad handshake)\n";
    ws_session.disconnect();
    return false;
}

/**
 * Serialises @p data to a JSON text frame and broadcasts it to every active
 * WsSession via `io_handler::stream()`.
 *
 * `stream(text)` calls `*session << text` for every entry in the session map,
 * avoiding the manual loop and coupling to the internal map structure.
 * The single `MessageText` is re-used per call, so the JSON is serialised only
 * once regardless of how many clients are connected.
 */
void WebSocketHandler::broadcast_to_all(const qb::json &data) {
    qb::http::ws::MessageText text;
    text << data.dump();
    // Qualify to resolve ambiguity: WebSocketHandler inherits stream() both from
    // io_handler<WsSession> (the WS session pool) and from the Redis transport.
    qb::io::async::io_handler<WebSocketHandler, WsSession>::stream(text);
}

} // namespace actors
} // namespace taskmanager
