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
#include <qbm/redis/redis.h>
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

    /**
     * @brief Drain published messages forever, broadcasting each to WS clients.
     *
     * NOTHING AFTER THE LOOP MAY TOUCH `this`, AND THAT IS LOAD-BEARING. The loop ends when
     * the message channel closes, and the only thing that closes it here is
     * `~RedisCoroConsumer` — running as part of the owning actor's destruction. `close()`
     * SCHEDULES a resume for every parked receiver, so the loop resumes with `std::nullopt`
     * *after* `~AuctionManager`, with `this` (which is `&_ws_handler`, a member of that
     * actor) already freed. The framework anticipates the parked receiver outliving its
     * channel — `recv_awaiter` holds a `_ch_alive` flag and returns `nullopt` without
     * dereferencing the freed channel. It cannot anticipate the loop's tail reading its own
     * members, so a `client_count()` or `_manager` access added there is an immediate
     * use-after-free: measured, one member read at that point is an ASan heap-use-after-free
     * on every run.
     */
    qb::io::async::task<void> consume_loop();

    /**
     * @brief Drop the subscriber connection (idempotent).
     *
     * Does NOT end `consume_loop()`, despite the intuition: `disconnect()` only feeds the io
     * watcher a deferred event, and when the actor is killed in the same pass `~client()`
     * stops that watcher before it ever fires. Measured, not assumed.
     */
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
