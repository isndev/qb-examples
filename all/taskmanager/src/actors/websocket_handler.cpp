/**
 * @file src/actors/websocket_handler.cpp
 * @brief WebSocketHandler (coroutine Redis Pub/Sub) + WsSession implementations.
 */

#include "actors/websocket_handler.h"
#include "actors/task_manager.h"
#include <qb/io.h>
#include <qb/system/time.h>

namespace taskmanager {
namespace actors {

// ─── WsSession ────────────────────────────────────────────────────────────────

/**
 * Parses the inbound WebSocket text frame as JSON and replies with an ACK.
 * Malformed payloads are silently discarded (closing the socket is the only way
 * to signal a protocol error over WS, which would be too aggressive here).
 */
void WsSession::on(ws_protocol::message &&msg) {
    try {
        auto json = qb::json::parse(msg.ws.data().view());
        qb::io::cout() << "[WsSession " << id() << "] rx type="
                       << json.value("type", "unknown") << '\n';

        send_json({
            {"type",      "ack"},
            {"received",  json.value("type", "unknown")},
            {"timestamp", qb::unix_nanos(qb::wall_now())}
        });
    } catch (const std::exception &e) {
        qb::io::cerr() << "[WsSession " << id() << "] parse error: "
                       << e.what() << '\n';
    }
}

/** Serialises @p msg to a JSON string and sends it as a WebSocket text frame. */
void WsSession::send_json(const qb::json &msg) {
    qb::http::ws::MessageText text;
    text << msg.dump();
    *this << text;
}

// ─── WebSocketHandler ─────────────────────────────────────────────────────────

WebSocketHandler::WebSocketHandler(TaskManager &manager, qb::io::uri redis_uri)
    : _manager(manager)
    , _redis_uri(std::move(redis_uri))
    , _sub(_redis_uri) {}

/**
 * Connect the coroutine subscriber and subscribe to `tasks:events`.
 * Awaited from TaskManager::onInit() — a failure aborts the actor's activation.
 */
qb::io::async::task<bool> WebSocketHandler::connect_subscriber() {
    if (!co_await _sub.connect()) {
        qb::io::cerr() << "[WebSocketHandler] Redis SUB connect failed\n";
        co_return false;
    }
    auto sub = co_await _sub.subscribe(std::string{"tasks:events"});
    if (!sub.ok()) {
        qb::io::cerr() << "[WebSocketHandler] subscribe failed: " << sub.error() << '\n';
        co_return false;
    }
    qb::io::cout() << "[WebSocketHandler] subscribed to tasks:events\n";
    co_return true;
}

/**
 * Coroutine receive loop: pull every published message and fan it out to all WS
 * clients. `receive()` yields `std::nullopt` when the subscription closes
 * (shutdown() / disconnect), which ends the loop cleanly.
 */
qb::io::async::task<void> WebSocketHandler::consume_loop() {
    while (auto msg = co_await _sub.receive()) {
        try {
            auto data = qb::json::parse(msg->payload);
            qb::io::cout() << "[WebSocketHandler] broadcast action="
                           << data.value("action", "?")
                           << "  clients=" << client_count() << '\n';
            broadcast_to_all(data);
        } catch (const std::exception &e) {
            qb::io::cerr() << "[WebSocketHandler] malformed payload: "
                           << e.what() << '\n';
        }
    }
    qb::io::cout() << "[WebSocketHandler] consume loop ended\n";
}

/** Close the subscriber connection so consume_loop() returns. */
void WebSocketHandler::shutdown() {
    _sub.disconnect();
}

/** Called by io_handler whenever a new WsSession becomes fully active. */
void WebSocketHandler::on(WsSession &session) {
    qb::io::cout() << "[WebSocketHandler] WS session active: " << session.id()
                   << "  total=" << client_count() << '\n';
}

/**
 * Performs the HTTP → WebSocket protocol upgrade on the extracted socket:
 * register the socket as a WsSession, run the WS handshake (filling the 101
 * response), and write it on the wire. On a bad handshake the session is
 * disconnected to release the fd.
 */
bool WebSocketHandler::upgrade_connection(qb::io::tcp::socket    &&sock,
                                          const qb::http::Request &request,
                                          qb::http::Response      &response) {
    auto *ws_session = registerSession(std::move(sock));
    if (!ws_session) {
        qb::io::cerr() << "[WebSocketHandler] upgrade failed (session limit reached)\n";
        return false;
    }

    if (ws_session->switch_protocol<WsSession::ws_protocol>(
            *ws_session, request, response)) {
        *ws_session << response;
        qb::io::cout() << "[WebSocketHandler] upgrade OK: " << ws_session->id() << '\n';
        return true;
    }

    qb::io::cerr() << "[WebSocketHandler] upgrade failed (bad handshake)\n";
    ws_session->disconnect();
    return false;
}

/**
 * Serialises @p data once and streams it as a text frame to every WsSession.
 * `stream()` comes solely from the io_handler base now (the Redis subscriber is a
 * member, not a base), so there is no ambiguity.
 */
void WebSocketHandler::broadcast_to_all(const qb::json &data) {
    qb::http::ws::MessageText text;
    text << data.dump();
    this->stream(text);
}

} // namespace actors
} // namespace taskmanager
