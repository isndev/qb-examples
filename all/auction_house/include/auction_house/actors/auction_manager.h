/**
 * @file actors/auction_manager.h
 * @brief AuctionManager — QB Actor + HTTP io_handler + DB + Redis + WS (coroutine-first).
 *
 * Architecture per VirtualCore:
 * - HTTP sessions (io_handler<HttpSession>)
 * - WebSocketHandler (inner component: WS pool + coroutine Redis subscriber)
 * - PostgreSQL connection (co_await SQL)
 * - Redis cache + PUBLISH client (co_await)
 *
 * ## Coroutine-first lifecycle
 * `onInit()` is a coroutine: it `co_await`s the PostgreSQL connection + prepared
 * statements, the Redis client, and the WebSocket subscriber before activating.
 * While suspended the actor is *Activating* — inbound connections are stashed and
 * replayed once ready (discover-before-activate). The schema itself is bootstrapped
 * once at process start (see main.cpp), so workers only connect + prepare.
 *
 * Every route handler is a coroutine (a `task<void>(ctx)` lambda passed directly
 * to the router) that `co_await`s the database and Redis directly — the bidding
 * path becomes a single linear transaction instead of nested callbacks.
 */
#pragma once

#include <filesystem>
#include <qbm/http/http.h>
#include <memory>
#include <pgsql/pgsql.h>
#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include "auction_house/events.h"
#include "auction_house/models/bid.h"
#include "auction_house/models/lot.h"
#include "auction_house/models/user.h"
#include "http_session.h"
#include "websocket_handler.h"

namespace auction_house {
namespace actors {

/**
 * @brief Main application actor — one independent instance per VirtualCore.
 */
class AuctionManager
    : public qb::Actor
    , public qb::http::use<AuctionManager>::io_handler<HttpSession> {
public:
    using ctx_t = std::shared_ptr<qb::http::Context<HttpSession>>;

    AuctionManager(qb::io::uri pg_uri, qb::io::uri redis_uri, std::filesystem::path static_root);

    // ── Lifecycle ───────────────────────────────────────────────────────────

    qb::io::async::task<bool> onInit() override;

    void on(events::NewConnectionEvent &ev);
    void on(qb::SignalEvent const &ev);
    void on(qb::KillEvent const &);
    void disconnected(qb::uuid session_id);

private:
    // ── Coroutine init helpers ────────────────────────────────────────────────

    qb::io::async::task<bool> prepare_statements();
    void                      setup_routes();
    void                      shutdown_resources();

    // ── Coroutine route handlers ──────────────────────────────────────────────

    qb::io::async::task<void> handle_health(ctx_t ctx);
    qb::io::async::task<void> handle_ws_upgrade(ctx_t ctx);
    qb::io::async::task<void> handle_list_lots(ctx_t ctx);
    qb::io::async::task<void> handle_get_lot(ctx_t ctx);
    qb::io::async::task<void> handle_get_lot_bids(ctx_t ctx);
    qb::io::async::task<void> handle_place_bid(ctx_t ctx);
    qb::io::async::task<void> handle_list_users(ctx_t ctx);
    qb::io::async::task<void> handle_get_user(ctx_t ctx);
    qb::io::async::task<void> handle_get_user_stats(ctx_t ctx);

    // ── Coroutine helpers ─────────────────────────────────────────────────────

    qb::io::async::task<void> broadcast_lot_event(models::LotEvent event);
    qb::io::async::task<void> invalidate_lot_cache(int32_t lot_id);

    // ── Members ───────────────────────────────────────────────────────────────

    qb::io::uri           _pg_uri;
    qb::io::uri           _redis_uri;
    std::filesystem::path _static_root;

    std::unique_ptr<qb::pg::tcp::database> _db;
    qb::redis::tcp::client                 _redis;
    WebSocketHandler                       _ws_handler;

    bool _db_ready{false};
    bool _redis_ready{false};
};

} // namespace actors
} // namespace auction_house
