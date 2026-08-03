/**
 * @file actors/websocket_handler.h
 * @brief WebSocketHandler — WS session pool + coroutine Redis Pub/Sub.
 *
 * Not an actor — shares the VirtualCore with AuctionManager. Owns the WS session
 * pool and a coroutine Redis subscriber (`co_consumer`): the owner drives a
 * `while (auto m = co_await receive()) broadcast(...)` loop instead of a callback.
 */
#pragma once

#include <qbm/http/http.h>
#include <qbm/http/ws.h>
#include <redis/redis.h>
#include <qb/io/async.h>
#include <qb/json.h>
#include "ws_session.h"

namespace auction_house {
namespace actors {

class AuctionManager; // forward declaration

/**
 * @brief Inner component: WebSocket session pool + coroutine Redis subscriber.
 */
class WebSocketHandler : public qb::io::use<WebSocketHandler>::tcp::io_handler<WsSession> {
public:
    WebSocketHandler(AuctionManager &manager, qb::io::uri redis_uri);

    // ── Coroutine lifecycle ──────────────────────────────────────────────────

    /** Connect the subscriber and subscribe to `auction:events`. */
    qb::io::async::task<bool> connect_subscriber();

    /** Drain published messages forever, broadcasting each to WS clients. */
    qb::io::async::task<void> consume_loop();

    /** Close the subscriber so `consume_loop()` returns (idempotent). */
    void shutdown();

    // ── Callbacks ────────────────────────────────────────────────────────────

    /** Called when a new WsSession becomes active (logging only). */
    void on(WsSession &session);

    // ── API ────────────────────────────────────────────────────────────────

    /** HTTP → WebSocket protocol upgrade on the extracted socket. */
    bool upgrade_connection(qb::io::tcp::socket &&sock, const qb::http::Request &request, qb::http::Response &response);

    /** Broadcast JSON to all connected WebSocket clients. */
    void broadcast_to_all(const qb::json &data);

    /** Number of connected clients. */
    [[nodiscard]] std::size_t
    client_count() const noexcept {
        return session_count();
    }

    /** Handle an inbound message from a WebSocket client (ping / subscribe_lot). */
    void handle_ws_message(const qb::json &data, WsSession &session);

private:
    AuctionManager             &_manager;
    qb::io::uri                 _redis_uri;
    qb::redis::tcp::co_consumer _sub; ///< coroutine Pub/Sub subscriber
};

} // namespace actors
} // namespace auction_house
