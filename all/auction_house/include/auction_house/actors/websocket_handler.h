/**
 * @file actors/websocket_handler.h
 * @brief WebSocketHandler - Inner component for WS sessions + Redis Pub/Sub.
 *
 * Not an actor - shares VirtualCore with AuctionManager.
 * Manages WebSocket session pool and broadcasts Redis messages.
 */
#pragma once

#include <redis/redis.h>
#include <http/http.h>
#include <ws/ws.h>
#include <qb/json.h>
#include "ws_session.h"

namespace auction_house {
namespace actors {

// Forward declaration
class AuctionManager;

/**
 * @brief Inner component: WebSocket session pool + Redis subscriber.
 *
 * Inherits from:
 * - io_handler<WsSession>: Manages WS session pool
 * - redis::tcp::consumer: Subscribes to Redis Pub/Sub
 */
class WebSocketHandler
    : public qb::io::use<WebSocketHandler>::tcp::io_handler<WsSession>
    , public qb::redis::tcp::consumer<WebSocketHandler> {
public:
    WebSocketHandler(AuctionManager &manager, qb::io::uri redis_uri);

    /**
     * @brief Connect to Redis and subscribe to auction:events.
     */
    bool init();

    // ── Callbacks ──────────────────────────────────────────────────────────

    /** Called when new WsSession becomes active. */
    void on(WsSession &session);

    /** Called on Redis Pub/Sub message. Broadcasts to all WS clients. */
    void on(qb::redis::message &&msg);

    // ── API ────────────────────────────────────────────────────────────────

    /**
     * @brief HTTP → WebSocket protocol upgrade.
     *
     * @param sock Extracted TCP socket (ownership transferred)
     * @param request Original HTTP upgrade request
     * @param response HTTP response (populated with 101 headers)
     * @return true on success
     */
    bool upgrade_connection(qb::io::tcp::socket &&sock,
                            const qb::http::Request &request,
                            qb::http::Response &response);

    /**
     * @brief Broadcast JSON to all connected WebSocket clients.
     */
    void broadcast_to_all(const qb::json &data);

    /** Get number of connected clients. */
    [[nodiscard]] std::size_t client_count() const noexcept {
        return session_count();
    }

    /** Handle incoming WebSocket message from client. */
    void handle_ws_message(const qb::json &data, WsSession &session);

private:
    AuctionManager &_manager;
    qb::io::uri _redis_uri;
};

} // namespace actors
} // namespace auction_house
