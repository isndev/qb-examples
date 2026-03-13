/**
 * @file actors/auction_manager.h
 * @brief AuctionManager - Main actor handling HTTP API + DB + Redis + WS.
 *
 * Architecture per VirtualCore:
 * - HTTP sessions (io_handler<HttpSession>)
 * - WebSocketHandler (inner component with WS pool + Redis sub)
 * - PostgreSQL connection
 * - Redis cache client
 */
#pragma once

#include <qb/actor.h>
#include <http/http.h>
#include <pgsql/pgsql.h>
#include <redis/redis.h>
#include <memory>
#include "auction_house/events.h"
#include "auction_house/models/lot.h"
#include "auction_house/models/bid.h"
#include "auction_house/models/user.h"
#include "http_session.h"
#include "websocket_handler.h"

namespace auction_house {
namespace actors {

/**
 * @brief Main application actor.
 *
 * One instance per VirtualCore. Handles:
 * - HTTP REST API for lots/bids
 * - PostgreSQL database operations
 * - Redis caching and Pub/Sub publishing
 * - WebSocket upgrades via WebSocketHandler
 */
class AuctionManager
    : public qb::Actor
    , public qb::http::use<AuctionManager>::io_handler<HttpSession> {
public:
    using ctx_t = std::shared_ptr<qb::http::Context<HttpSession>>;

    AuctionManager(qb::io::uri pg_uri,
                   qb::io::uri redis_uri,
                   std::string static_root);

    // ── Lifecycle ───────────────────────────────────────────────────────────

    bool onInit() final;

    // ── Event Handlers ───────────────────────────────────────────────────

    /** Accept TCP connection from TcpListener. */
    void on(events::NewConnectionEvent &ev);

    /** Handle OS signal (SIGINT/SIGTERM). */
    void on(qb::SignalEvent const &ev);

    /** Graceful shutdown. */
    void on(qb::KillEvent const &);

    /** Called when HttpSession disconnects. */
    void disconnected(qb::uuid session_id);

private:
    // ── Initialization ──────────────────────────────────────────────────────

    /** Initialize database connection and prepare statements. */
    void init_database();

    /** Prepare SQL statements for common queries. */
    bool prepare_statements();

    void init_redis();
    void init_websocket_handler();
    void setup_routes();

    // ── Route Handlers ───────────────────────────────────────────────────

    void handle_health(ctx_t ctx);
    void handle_ws_upgrade(ctx_t ctx);

    // Lots API
    void handle_list_lots(ctx_t ctx);
    void handle_get_lot(ctx_t ctx);
    void handle_get_lot_bids(ctx_t ctx);

    // Bids API
    void handle_place_bid(ctx_t ctx);

    // Users API
    void handle_list_users(ctx_t ctx);
    void handle_get_user(ctx_t ctx);
    void handle_get_user_stats(ctx_t ctx);

    // ── Helpers ───────────────────────────────────────────────────────────

    void broadcast_lot_event(const models::LotEvent &event);
    [[nodiscard]] bool is_lot_active(int32_t lot_id);
    void invalidate_lot_cache(int32_t lot_id);

    // ── Members ───────────────────────────────────────────────────────────

    qb::io::uri _pg_uri;
    qb::io::uri _redis_uri;
    std::string _static_root;

    std::unique_ptr<qb::pg::tcp::database> _db;
    qb::redis::tcp::client _redis;
    WebSocketHandler _ws_handler;

    bool _db_ready{false};
    bool _redis_ready{false};
    bool _ws_ready{false};
};

} // namespace actors
} // namespace auction_house
