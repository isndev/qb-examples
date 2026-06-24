/**
 * @file src/actors/auction_manager.cpp
 * @brief AuctionManager coroutine lifecycle, HTTP API, DB, Redis, WebSocket.
 *
 * Everything network-facing is a coroutine. The bidding path — insert bid +
 * update lot price + commit — is a single linear transaction instead of the
 * former three nested callbacks.
 *
 * The schema is bootstrapped once at process start (main.cpp::init_db); each
 * worker only connects and prepares statements here.
 */

#include "auction_house/actors/auction_manager.h"
#include <ctime>
#include <http/middleware/all.h>
#include <iomanip>
#include <sstream>
#include <qb/io.h>

namespace auction_house {
namespace actors {

// ─── Constructor ────────────────────────────────────────────────────────────

AuctionManager::AuctionManager(qb::io::uri pg_uri, qb::io::uri redis_uri, std::string static_root)
    : _pg_uri(std::move(pg_uri))
    , _redis_uri(std::move(redis_uri))
    , _static_root(std::move(static_root))
    , _redis(_redis_uri)
    , _ws_handler(*this, _redis_uri) {}

// ─── Coroutine lifecycle ────────────────────────────────────────────────────

qb::io::async::task<bool>
AuctionManager::onInit() {
    registerEvent<events::NewConnectionEvent>(*this);
    registerEvent<qb::KillEvent>(*this);
    registerEvent<qb::SignalEvent>(*this);

    // 1. PostgreSQL — connect + prepare (schema already bootstrapped by main).
    _db = std::make_unique<qb::pg::tcp::database>();
    if (!co_await _db->connect(_pg_uri.source())) {
        qb::io::cerr() << "[AuctionManager " << id() << "] DB connect failed\n";
        co_return false;
    }
    if (!co_await prepare_statements()) {
        qb::io::cerr() << "[AuctionManager " << id() << "] prepare failed\n";
        co_return false;
    }
    _db_ready = true;

    // 2. Redis cache + PUBLISH client.
    if (!co_await _redis.connect()) {
        qb::io::cerr() << "[AuctionManager " << id() << "] Redis connect failed\n";
        co_return false;
    }
    _redis_ready = true;

    // 3. WebSocket Redis subscriber + actor-scoped consume loop.
    if (!co_await _ws_handler.connect_subscriber())
        co_return false;
    spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
        co_await _ws_handler.consume_loop(); // ends on shutdown(); cancelled on kill
    });

    // 4. Routes.
    setup_routes();
    router().compile();

    qb::io::cout() << "[AuctionManager " << id() << "] ready (coroutine init) — db + redis + ws all up\n";
    co_return true;
}

void
AuctionManager::on(events::NewConnectionEvent &ev) {
    auto *session = registerSession(std::move(ev.socket));
    if (!session) {
        qb::io::cerr() << "[AuctionManager " << id() << "] HTTP session rejected (limit reached)\n";
        return;
    }
    qb::io::cout() << "[AuctionManager " << id() << "] HTTP session " << session->id() << " (active: " << session_count() << ")\n";
}

void
AuctionManager::on(qb::SignalEvent const &) {
    qb::io::cout() << "[AuctionManager " << id() << "] signal — shutting down\n";
    shutdown_resources();
    kill();
}

void
AuctionManager::on(qb::KillEvent const &) {
    qb::io::cout() << "[AuctionManager " << id() << "] shutdown\n";
    shutdown_resources();
    kill();
}

void
AuctionManager::shutdown_resources() {
    _ws_handler.shutdown();
    _redis.disconnect();
    if (_db)
        _db->disconnect();
}

void
AuctionManager::disconnected(qb::uuid session_id) {
    qb::io::cout() << "[AuctionManager " << id() << "] session " << session_id << " disconnected (remaining: " << (session_count() - 1)
                   << ")\n";
    qb::http::use<AuctionManager>::io_handler<HttpSession>::disconnected(session_id);
}

// ─── Prepared statements (coroutine) ──────────────────────────────────────────

qb::io::async::task<bool>
AuctionManager::prepare_statements() {
    using qb::pg::oid;
    const auto prep = [this](std::string_view name, std::string_view sql, qb::pg::type_oid_sequence types) -> qb::io::async::task<bool> {
        auto r = co_await _db->prepare(std::string{name}, std::string{sql}, std::move(types));
        if (!r.ok())
            qb::io::cerr() << "[AuctionManager] prepare '" << name << "' failed: " << r.error().what() << "\n";
        co_return r.ok();
    };

    // NUMERIC → float8 and TIMESTAMP → text so the binary results map cleanly to
    // the models' double / std::string fields (raw NUMERIC/TIMESTAMP decode to 0/empty).
    const std::string lot_cols = " l.id, l.title, l.description, l.category, l.image_url,"
                                 " l.start_price::float8   AS start_price,"
                                 " l.current_price::float8 AS current_price,"
                                 " l.reserve_price::float8 AS reserve_price,"
                                 " l.seller_id, l.status,"
                                 " EXTRACT(EPOCH FROM l.start_time)::bigint AS start_time,"
                                 " EXTRACT(EPOCH FROM l.end_time)::bigint AS end_time,"
                                 " l.created_at::text AS created_at, l.updated_at::text AS updated_at,"
                                 " COALESCE((SELECT COUNT(*) FROM bids b WHERE b.lot_id = l.id), 0) AS bid_count ";

    if (!co_await prep("select_active_lots",
                       "SELECT" + lot_cols
                           + "FROM lots l WHERE l.status = 'active' AND l.end_time > NOW()"
                             " ORDER BY l.end_time ASC LIMIT 100;",
                       qb::pg::type_oid_sequence{}))
        co_return false;

    if (!co_await prep("select_lot_by_id", "SELECT" + lot_cols + "FROM lots l WHERE l.id = $1;", qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    if (!co_await prep("select_lot_bids",
                       "SELECT b.id, b.lot_id, b.bidder_id, b.amount::float8 AS amount,"
                       "       b.bid_time::text AS bid_time, b.is_winning,"
                       "       u.username AS bidder_username"
                       " FROM bids b JOIN users u ON b.bidder_id = u.id"
                       " WHERE b.lot_id = $1 ORDER BY b.bid_time DESC;",
                       qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    const std::string user_cols = " id, username, email, balance::float8 AS balance, created_at::text AS created_at ";

    if (!co_await prep("select_all_users", "SELECT" + user_cols + "FROM users ORDER BY id ASC;", qb::pg::type_oid_sequence{}))
        co_return false;

    if (!co_await prep("select_user_by_id", "SELECT" + user_cols + "FROM users WHERE id = $1;", qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    if (!co_await prep("insert_bid",
                       "INSERT INTO bids (lot_id, bidder_id, amount)"
                       " VALUES ($1, $2, $3::numeric) RETURNING id, bid_time;",
                       qb::pg::type_oid_sequence{oid::int4, oid::int4, oid::text}))
        co_return false;

    if (!co_await prep("update_lot_price",
                       "UPDATE lots SET current_price = $1::numeric, updated_at = NOW()"
                       " WHERE id = $2 AND end_time > NOW()"
                       " RETURNING id, title, description, category, image_url,"
                       "   start_price::float8 AS start_price,"
                       "   current_price::float8 AS current_price,"
                       "   reserve_price::float8 AS reserve_price,"
                       "   seller_id, status,"
                       "   EXTRACT(EPOCH FROM start_time)::bigint AS start_time,"
                       "   EXTRACT(EPOCH FROM end_time)::bigint AS end_time,"
                       "   created_at::text AS created_at, updated_at::text AS updated_at;",
                       qb::pg::type_oid_sequence{oid::text, oid::int4}))
        co_return false;

    if (!co_await prep("select_user_stats",
                       "SELECT"
                       "  COUNT(DISTINCT b.id) AS total_bids,"
                       "  COUNT(DISTINCT CASE WHEN l.end_time > NOW() AND l.status = 'active'"
                       "    AND b.amount = l.current_price THEN b.lot_id END) AS active_auctions,"
                       "  COUNT(DISTINCT ar.lot_id) AS auctions_won"
                       " FROM bids b"
                       " LEFT JOIN lots l ON b.lot_id = l.id"
                       " LEFT JOIN auction_results ar ON ar.lot_id = b.lot_id AND ar.winner_id = $1"
                       " WHERE b.bidder_id = $1;",
                       qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    co_return true;
}

// ─── Routes ─────────────────────────────────────────────────────────────────

void
AuctionManager::setup_routes() {
    router().use(qb::http::CorsMiddleware<HttpSession>::dev());
    router().use(std::make_shared<qb::http::LoggingMiddleware<HttpSession>>(
        [](qb::http::LogLevel, const std::string &msg) { qb::io::cout() << "[HTTP] " << msg << "\n"; }, qb::http::LogLevel::Info,
        qb::http::LogLevel::Warning));
    {
        qb::http::StaticFilesOptions opts(_static_root);
        opts.with_path_prefix_to_strip("/static").with_etags(true).with_last_modified(true).with_cache_control(true, "public, max-age=3600");
        router().use(qb::http::static_files_middleware<HttpSession>(std::move(opts)));
    }

    // Coroutine member handlers registered directly — no wrapper, no session type.
    router().get("/", [](ctx_t ctx) { ctx->redirect("/static/index.html"); });
    router().get("/health", this, &AuctionManager::handle_health);
    router().get("/ws", this, &AuctionManager::handle_ws_upgrade);

    router().get("/api/lots", this, &AuctionManager::handle_list_lots);
    router().get("/api/lots/:id", this, &AuctionManager::handle_get_lot);
    router().get("/api/lots/:id/bids", this, &AuctionManager::handle_get_lot_bids);
    router().post("/api/lots/:id/bids", this, &AuctionManager::handle_place_bid);

    router().get("/api/users", this, &AuctionManager::handle_list_users);
    router().get("/api/users/:id", this, &AuctionManager::handle_get_user);
    router().get("/api/users/:id/stats", this, &AuctionManager::handle_get_user_stats);
}

// ─── Coroutine route handlers ───────────────────────────────────────────────

qb::io::async::task<void>
AuctionManager::handle_health(ctx_t ctx) {
    ctx->json(qb::json{
        {"status", "ok"},
        {"service", "auction_house"},
        {"version", "1.0.0"},
        {"db_ready", _db_ready},
        {"redis_ready", _redis_ready},
        {"ws_clients", _ws_handler.client_count()},
        {"timestamp", std::time(nullptr)}
    });
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_ws_upgrade(ctx_t ctx) {
    try {
        auto [sock, ok] = this->extractSession(ctx->session()->id());
        if (!ok) {
            ctx->internal_server_error("Session extraction failed");
            co_return;
        }
        if (_ws_handler.upgrade_connection(std::move(sock), ctx->request(), ctx->response()))
            ctx->suppress_response();
        else
            ctx->bad_request("WebSocket upgrade failed");
    } catch (const std::exception &e) {
        qb::io::cerr() << "[AuctionManager] WS upgrade exception: " << e.what() << "\n";
        ctx->internal_server_error("WebSocket upgrade error");
    }
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_list_lots(ctx_t ctx) {
    auto cached = co_await _redis.get("lots:active");
    if (cached.ok() && cached.result().has_value() && !cached.result()->empty()) {
        ctx->response().add_header("X-Cache", "HIT");
        ctx->json(qb::json::parse(*cached.result()));
        co_return;
    }

    auto res = co_await _db->execute("select_active_lots", qb::pg::params{});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    auto json = models::LotList(res.result()).to_json();
    (void) co_await _redis.setex("lots:active", 30LL, json.dump()); // best-effort cache

    ctx->response().add_header("X-Cache", "MISS");
    ctx->json(json);
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_get_lot(ctx_t ctx) {
    const auto lot_id_opt = ctx->path_param<int32_t>("id");
    if (!lot_id_opt) {
        ctx->bad_request("Invalid lot id");
        co_return;
    }
    const int32_t lot_id = *lot_id_opt;

    const std::string key    = "lot:" + std::to_string(lot_id);
    auto              cached = co_await _redis.get(key);
    if (cached.ok() && cached.result().has_value() && !cached.result()->empty()) {
        try {
            auto json = qb::json::parse(*cached.result());
            if (json.contains("id") && json["id"] == lot_id) {
                ctx->response().add_header("X-Cache", "HIT");
                ctx->json(json);
                co_return;
            }
        } catch (const std::exception &) { /* stale cache → fall through to DB */
        }
    }

    auto res = co_await _db->execute("select_lot_by_id", qb::pg::params{lot_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    if (res.result().empty()) {
        ctx->not_found("Lot not found");
        co_return;
    }
    auto json = models::Lot(res.result()[0]).to_json();
    (void) co_await _redis.setex(key, 60LL, json.dump()); // best-effort cache

    ctx->response().add_header("X-Cache", "MISS");
    ctx->json(json);
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_get_lot_bids(ctx_t ctx) {
    const auto lot_id_opt = ctx->path_param<int32_t>("id");
    if (!lot_id_opt) {
        ctx->bad_request("Invalid lot id");
        co_return;
    }
    const int32_t lot_id = *lot_id_opt;

    auto res = co_await _db->execute("select_lot_bids", qb::pg::params{lot_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    ctx->json(models::BidHistory(lot_id, res.result()).to_json());
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_place_bid(ctx_t ctx) {
    const auto lot_id_opt = ctx->path_param<int32_t>("id");
    if (!lot_id_opt) {
        ctx->bad_request("Invalid lot id");
        co_return;
    }
    const int32_t lot_id = *lot_id_opt;

    double  amount;
    int32_t bidder_id;
    try {
        auto json = qb::json::parse(ctx->request().body().as<std::string>());
        amount    = json.value("amount", 0.0);
        bidder_id = json.value("bidder_id", 0);
        if (amount <= 0 || bidder_id <= 0) {
            ctx->bad_request("Invalid bid amount or bidder_id");
            co_return;
        }
    } catch (const std::exception &e) {
        ctx->bad_request(e.what());
        co_return;
    }

    // NUMERIC columns: pass the amount as a formatted string (binary double is lossy).
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount;
    const std::string amount_str = oss.str();

    // One linear transaction: insert the bid, bump the lot price, commit.
    if (!(co_await _db->begin()).ok()) {
        ctx->internal_server_error("Could not begin transaction");
        co_return;
    }

    auto ins = co_await _db->execute("insert_bid", qb::pg::params{lot_id, bidder_id, amount_str});
    if (!ins.ok() || ins.result().empty()) {
        (void) co_await _db->rollback();
        ctx->json({{"error", "Bid failed - lot may have ended"}}, qb::http::status::CONFLICT);
        co_return;
    }
    const int32_t bid_id = ins.result()[0]["id"].as<int32_t>();

    auto upd = co_await _db->execute("update_lot_price", qb::pg::params{amount_str, lot_id});
    if (!upd.ok() || upd.result().empty()) {
        (void) co_await _db->rollback(); // lot ended between insert and update
        ctx->json({{"error", "Bid failed - lot may have ended"}}, qb::http::status::CONFLICT);
        co_return;
    }
    if (!(co_await _db->commit()).ok()) {
        ctx->internal_server_error("Commit failed");
        co_return;
    }

    models::Lot lot(upd.result()[0]);
    co_await invalidate_lot_cache(lot_id);

    models::BidResult result;
    result.success   = true;
    result.message   = "Bid placed successfully";
    result.bid_id    = bid_id;
    result.new_price = amount;
    result.time_left = lot.time_left;
    ctx->json(result.to_json(), qb::http::status::CREATED);

    // Response is already sent — resolve the bidder name and broadcast the event.
    auto             user = co_await _db->execute("select_user_by_id", qb::pg::params{bidder_id});
    models::LotEvent event;
    event.action    = "bid";
    event.lot_id    = lot_id;
    event.new_price = amount;
    event.bidder    = (user.ok() && !user.result().empty()) ? user.result()[0]["username"].as<std::string>() : std::to_string(bidder_id);
    event.time_left = lot.time_left;
    event.timestamp = std::time(nullptr);
    co_await broadcast_lot_event(std::move(event));
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_list_users(ctx_t ctx) {
    auto res = co_await _db->execute("select_all_users", qb::pg::params{});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    qb::json::array_t users_arr;
    for (const auto &row : res.result())
        users_arr.push_back(models::User(row).to_json());
    ctx->json(qb::json{{"users", users_arr}, {"total", res.result().size()}});
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_get_user(ctx_t ctx) {
    const auto user_id_opt = ctx->path_param<int32_t>("id");
    if (!user_id_opt) {
        ctx->bad_request("Invalid user id");
        co_return;
    }
    const int32_t user_id = *user_id_opt;

    auto res = co_await _db->execute("select_user_by_id", qb::pg::params{user_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    if (res.result().empty()) {
        ctx->not_found("User not found");
        co_return;
    }
    ctx->json(models::User(res.result()[0]).to_json());
    co_return;
}

qb::io::async::task<void>
AuctionManager::handle_get_user_stats(ctx_t ctx) {
    const auto user_id_opt = ctx->path_param<int32_t>("id");
    if (!user_id_opt) {
        ctx->bad_request("Invalid user id");
        co_return;
    }
    const int32_t user_id = *user_id_opt;

    auto user = co_await _db->execute("select_user_by_id", qb::pg::params{user_id});
    if (!user.ok()) {
        ctx->internal_server_error(user.error().what());
        co_return;
    }
    if (user.result().empty()) {
        ctx->not_found("User not found");
        co_return;
    }

    models::UserStats stats;
    stats.user_id  = user_id;
    stats.username = user.result()[0]["username"].as<std::string>().c_str();

    auto sres = co_await _db->execute("select_user_stats", qb::pg::params{user_id});
    if (sres.ok() && !sres.result().empty()) {
        stats.total_bids      = sres.result()[0]["total_bids"].as<int32_t>();
        stats.active_auctions = sres.result()[0]["active_auctions"].as<int32_t>();
        stats.auctions_won    = sres.result()[0]["auctions_won"].as<int32_t>();
    }
    ctx->json(stats.to_json());
    co_return;
}

// ─── Coroutine helpers ─────────────────────────────────────────────────────────

qb::io::async::task<void>
AuctionManager::broadcast_lot_event(models::LotEvent event) {
    (void) co_await _redis.publish("auction:events", event.to_json().dump()); // best-effort notify
}

qb::io::async::task<void>
AuctionManager::invalidate_lot_cache(int32_t lot_id) {
    (void) co_await _redis.del("lots:active"); // best-effort cache invalidation
    (void) co_await _redis.del("lot:" + std::to_string(lot_id));
    (void) co_await _redis.del("lot:" + std::to_string(lot_id) + ":bids");
}

} // namespace actors
} // namespace auction_house
