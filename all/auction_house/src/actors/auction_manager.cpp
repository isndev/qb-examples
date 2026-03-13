/**
 * @file src/actors/auction_manager.cpp
 * @brief AuctionManager implementation - HTTP API, DB, Redis, WebSocket.
 */

#include "auction_house/actors/auction_manager.h"
#include <http/middleware/all.h>
#include <qb/io.h>
#include <iomanip>
#include <sstream>

namespace auction_house {
namespace actors {

// ─── Constructor ────────────────────────────────────────────────────────────

AuctionManager::AuctionManager(qb::io::uri pg_uri,
                               qb::io::uri redis_uri,
                               std::string static_root)
    : _pg_uri(std::move(pg_uri))
    , _redis_uri(std::move(redis_uri))
    , _static_root(std::move(static_root))
    , _redis(_redis_uri)
    , _ws_handler(*this, _redis_uri) {}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool AuctionManager::onInit() {
    registerEvent<events::NewConnectionEvent>(*this);
    registerEvent<qb::KillEvent>(*this);
    registerEvent<qb::SignalEvent>(*this);

    init_database();
    init_redis();
    init_websocket_handler();
    setup_routes();
    router().compile();

    qb::io::cout() << "[AuctionManager " << id() << "] Ready: "
                  << "db=" << _db_ready
                  << " redis=" << _redis_ready
                  << " ws=" << _ws_ready << "\n";

    // Require at least DB to be ready
    return _db_ready;
}

void AuctionManager::on(events::NewConnectionEvent &ev) {
    auto &session = registerSession(std::move(ev.socket));
    qb::io::cout() << "[AuctionManager " << id() << "] HTTP session "
                  << session.id() << " (active: " << session_count() << ")\n";
}

void AuctionManager::on(qb::SignalEvent const &) {
    qb::io::cout() << "[AuctionManager " << id() << "] Signal received - shutting down\n";
    _redis.quit();
    if (_db) _db->disconnect();
    kill();
}

void AuctionManager::on(qb::KillEvent const &) {
    qb::io::cout() << "[AuctionManager " << id() << "] Shutdown\n";
    _redis.quit();
    if (_db) _db->disconnect();
    kill();
}

void AuctionManager::disconnected(qb::uuid session_id) {
    qb::io::cout() << "[AuctionManager " << id() << "] Session " << session_id
                  << " disconnected (remaining: " << (session_count() - 1) << ")\n";

    // CRITICAL: Call base to free session resources
    qb::http::use<AuctionManager>::io_handler<HttpSession>::disconnected(session_id);
}

// ─── Initialization ──────────────────────────────────────────────────────────

void AuctionManager::init_database() {
    try {
        _db = std::make_unique<qb::pg::tcp::database>(_pg_uri.source());
        if (!_db->connect()) {
            qb::io::cerr() << "[AuctionManager " << id() << "] DB connect failed\n";
            return;
        }

        if (!prepare_statements()) {
            qb::io::cerr() << "[AuctionManager " << id() << "] Prepare failed\n";
            return;
        }
        _db_ready = true;
        qb::io::cout() << "[AuctionManager " << id() << "] DB connected\n";
    } catch (const std::exception &e) {
        qb::io::cerr() << "[AuctionManager " << id() << "] DB error: " << e.what() << "\n";
    }
}

bool AuctionManager::prepare_statements() {
    if (!_db) return false;

    auto ret = _db->begin([](qb::pg::transaction &tr) {
        tr.prepare("select_active_lots",
            "SELECT l.id, l.title, l.description, l.category, l.image_url, "
            "  l.start_price, l.current_price, l.reserve_price, l.seller_id, l.status, "
            "  EXTRACT(EPOCH FROM l.start_time)::bigint AS start_time, "
            "  EXTRACT(EPOCH FROM l.end_time)::bigint AS end_time, "
            "  l.created_at::text, l.updated_at::text, "
            "  COALESCE((SELECT COUNT(*) FROM bids b WHERE b.lot_id = l.id), 0) AS bid_count "
            "FROM lots l WHERE l.status = 'active' AND l.end_time > NOW() "
            "ORDER BY l.end_time ASC LIMIT 100;",
            {})
        .prepare("select_lot_by_id",
            "SELECT l.id, l.title, l.description, l.category, l.image_url, "
            "  l.start_price, l.current_price, l.reserve_price, l.seller_id, l.status, "
            "  EXTRACT(EPOCH FROM l.start_time)::bigint AS start_time, "
            "  EXTRACT(EPOCH FROM l.end_time)::bigint AS end_time, "
            "  l.created_at::text, l.updated_at::text, "
            "  COALESCE((SELECT COUNT(*) FROM bids b WHERE b.lot_id = l.id), 0) AS bid_count "
            "FROM lots l WHERE l.id = $1;",
            {qb::pg::oid::int4})
        .prepare("select_lot_bids",
            "SELECT b.*, u.username AS bidder_username FROM bids b "
            "JOIN users u ON b.bidder_id = u.id "
            "WHERE b.lot_id = $1 ORDER BY b.bid_time DESC;",
            {qb::pg::oid::int4})
        .prepare("select_all_users",
            "SELECT * FROM users ORDER BY id ASC;",
            {})
        .prepare("select_user_by_id",
            "SELECT * FROM users WHERE id = $1;",
            {qb::pg::oid::int4})
        .prepare("insert_bid",
            "INSERT INTO bids (lot_id, bidder_id, amount) VALUES ($1, $2, $3::numeric) "
            "RETURNING id, bid_time;",
            {qb::pg::oid::int4, qb::pg::oid::int4, qb::pg::oid::text})
        .prepare("update_lot_price",
            "UPDATE lots SET current_price = $1::numeric, updated_at = NOW() "
            "WHERE id = $2 AND end_time > NOW() "
            "RETURNING id, title, description, category, image_url, "
            "  start_price, current_price, reserve_price, seller_id, status, "
            "  EXTRACT(EPOCH FROM start_time)::bigint AS start_time, "
            "  EXTRACT(EPOCH FROM end_time)::bigint AS end_time, "
            "  created_at::text, updated_at::text;",
            {qb::pg::oid::text, qb::pg::oid::int4})
        .prepare("select_user_stats",
            "SELECT "
            "  COUNT(DISTINCT b.id) AS total_bids, "
            "  COUNT(DISTINCT CASE WHEN l.end_time > NOW() AND l.status = 'active' "
            "    AND b.amount = l.current_price THEN b.lot_id END) AS active_auctions, "
            "  COUNT(DISTINCT ar.lot_id) AS auctions_won "
            "FROM bids b "
            "LEFT JOIN lots l ON b.lot_id = l.id "
            "LEFT JOIN auction_results ar ON ar.lot_id = b.lot_id AND ar.winner_id = $1 "
            "WHERE b.bidder_id = $1;",
            {qb::pg::oid::int4});
    }).await();

    return ret();
}

void AuctionManager::init_redis() {
    if (!_redis.connect()) {
        qb::io::cerr() << "[AuctionManager " << id() << "] Redis connect failed\n";
        return;
    }
    _redis_ready = true;
    qb::io::cout() << "[AuctionManager " << id() << "] Redis connected\n";
}

void AuctionManager::init_websocket_handler() {
    if (!_ws_handler.init()) {
        qb::io::cerr() << "[AuctionManager " << id() << "] WS handler init failed\n";
        return;
    }
    _ws_ready = true;
    qb::io::cout() << "[AuctionManager " << id() << "] WebSocket ready\n";
}

void AuctionManager::setup_routes() {
    // CORS middleware
    router().use(qb::http::CorsMiddleware<HttpSession>::dev());

    // Logging middleware
    router().use(std::make_shared<qb::http::LoggingMiddleware<HttpSession>>(
        [](qb::http::LogLevel level, const std::string &msg) {
            qb::io::cout() << "[HTTP] " << msg << "\n";
        },
        qb::http::LogLevel::Info,
        qb::http::LogLevel::Warning
    ));

    // Static files
    {
        qb::http::StaticFilesOptions opts(_static_root);
        opts.with_path_prefix_to_strip("/static")
            .with_etags(true)
            .with_last_modified(true)
            .with_cache_control(true, "public, max-age=3600");
        router().use(qb::http::static_files_middleware<HttpSession>(std::move(opts)));
    }

    // Health check
    router().get("/health", [this](ctx_t ctx) { handle_health(ctx); });

    // Lots API
    router().get("/api/lots", [this](ctx_t ctx) { handle_list_lots(ctx); });
    router().get("/api/lots/:id", [this](ctx_t ctx) { handle_get_lot(ctx); });
    router().get("/api/lots/:id/bids", [this](ctx_t ctx) { handle_get_lot_bids(ctx); });

    // Bids API
    router().post("/api/lots/:id/bids", [this](ctx_t ctx) { handle_place_bid(ctx); });

    // Users API
    router().get("/api/users", [this](ctx_t ctx) { handle_list_users(ctx); });
    router().get("/api/users/:id", [this](ctx_t ctx) { handle_get_user(ctx); });
    router().get("/api/users/:id/stats", [this](ctx_t ctx) { handle_get_user_stats(ctx); });

    // WebSocket upgrade
    router().get("/ws", [this](ctx_t ctx) { handle_ws_upgrade(ctx); });

    // Redirect root to frontend
    router().get("/", [](ctx_t ctx) {
        ctx->redirect("/static/index.html");
    });
}

// ─── Route Handlers ───────────────────────────────────────────────────────────

void AuctionManager::handle_health(ctx_t ctx) {
    ctx->json(qb::json{
        {"status", "ok"},
        {"service", "auction_house"},
        {"version", "1.0.0"},
        {"db_ready", _db_ready},
        {"redis_ready", _redis_ready},
        {"ws_ready", _ws_ready},
        {"timestamp", std::time(nullptr)}
    });
}

void AuctionManager::handle_ws_upgrade(ctx_t ctx) {
    try {
        if (!_ws_ready) {
            // 501 Not Implemented - WebSocket not available
            ctx->json({{"error", "WebSocket not available"}}, qb::http::Status::NOT_IMPLEMENTED);
            return;
        }

        auto [sock, ok] = this->extractSession(ctx->session()->id());
        if (!ok) {
            ctx->internal_server_error("Session extraction failed");
            return;
        }

        if (_ws_handler.upgrade_connection(std::move(sock), ctx->request(), ctx->response())) {
            ctx->suppress_response();
        } else {
            ctx->bad_request("WebSocket upgrade failed");
        }
    } catch (const std::exception &e) {
        qb::io::cerr() << "[AuctionManager] WS upgrade exception: " << e.what() << '\n';
        ctx->internal_server_error("WebSocket upgrade error");
    }
}

void AuctionManager::handle_list_lots(ctx_t ctx) {
    // Check cache
    if (_redis_ready) {
        auto cached = _redis.get("lots:active");
        if (cached.has_value() && !cached->empty()) {
            ctx->response().add_header("X-Cache", "HIT");
            ctx->json(qb::json::parse(*cached));
            return;
        }
    }

    auto context = ctx;

    _db->execute("select_active_lots", {},
        [this, context](qb::pg::transaction &, qb::pg::resultset &&res) {
            models::LotList list(res);
            auto json = list.to_json();

            if (_redis_ready) {
                _redis.setex("lots:active", 30, json.dump());
            }

            context->response().add_header("X-Cache", "MISS");
            context->json(json);
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

void AuctionManager::handle_get_lot(ctx_t ctx) {
    auto lot_id = std::stoi(ctx->path_param("id"));

    // Check cache
    if (_redis_ready) {
        auto cached = _redis.get("lot:" + std::to_string(lot_id));
        if (cached.has_value() && !cached->empty()) {
            try {
                auto json = qb::json::parse(*cached);
                // Validate that it's a valid lot object with id
                if (json.contains("id") && json["id"] == lot_id) {
                    ctx->response().add_header("X-Cache", "HIT");
                    ctx->json(json);
                    return;
                }
                // Invalid cache entry, continue to DB
            } catch (const std::exception &) {
                // Invalid JSON in cache, continue to DB
            }
        }
    }

    auto context = ctx;

    _db->execute("select_lot_by_id", {lot_id},
        [this, context, lot_id](qb::pg::transaction &, qb::pg::resultset &&res) {
            if (res.empty()) {
                // Use helper method to properly set 404 status
                context->not_found("Lot not found");
                return;
            }

            models::Lot lot(res[0]);
            auto json = lot.to_json();

            if (_redis_ready) {
                _redis.setex("lot:" + std::to_string(lot_id), 60, json.dump());
            }

            context->response().add_header("X-Cache", "MISS");
            context->json(json);
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

void AuctionManager::handle_get_lot_bids(ctx_t ctx) {
    auto lot_id = std::stoi(ctx->path_param("id"));
    auto context = ctx;

    _db->execute("select_lot_bids", {lot_id},
        [context, lot_id](qb::pg::transaction &, qb::pg::resultset &&res) {
            models::BidHistory history(lot_id, res);
            context->json(history.to_json());
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

void AuctionManager::handle_place_bid(ctx_t ctx) {
    auto lot_id = std::stoi(ctx->path_param("id"));

    // Parse request body
    auto body = ctx->request().body().as<std::string>();
    double amount;
    int32_t bidder_id;

    try {
        auto json = qb::json::parse(body);
        amount = json.value("amount", 0.0);
        bidder_id = json.value("bidder_id", 0);

        if (amount <= 0 || bidder_id <= 0) {
            ctx->bad_request("Invalid bid amount or bidder_id");
            return;
        }
    } catch (const std::exception &e) {
        ctx->bad_request(e.what());
        return;
    }

    auto context = ctx;

    // Format amount as string — QB binary encoding of double is unreliable for NUMERIC columns
    std::ostringstream _oss;
    _oss << std::fixed << std::setprecision(2) << amount;
    std::string amount_str = _oss.str();

    // Insert bid and update lot price
    _db->execute("insert_bid", {lot_id, bidder_id, amount_str},
        [this, context, lot_id, amount, amount_str, bidder_id](qb::pg::transaction &tr, qb::pg::resultset &&res) {
            if (res.empty()) {
                context->internal_server_error("Bid insertion failed");
                return;
            }

            int32_t bid_id = res[0]["id"].as<int32_t>();

            // Update lot price (within same transaction via tr)
            tr.execute("update_lot_price", {amount_str, lot_id},
                [this, context, lot_id, amount, bidder_id, bid_id](qb::pg::transaction &, qb::pg::resultset &&lot_res) {
                    if (lot_res.empty()) {
                        // Lot ended between insert and update
                        context->json({{"error", "Bid failed - lot may have ended"}}, qb::http::Status::CONFLICT);
                        return;
                    }

                    models::Lot lot(lot_res[0]);

                    // Invalidate caches
                    invalidate_lot_cache(lot_id);

                    // Return success to client immediately
                    models::BidResult result;
                    result.success = true;
                    result.message = "Bid placed successfully";
                    result.bid_id = bid_id;
                    result.new_price = amount;
                    result.time_left = lot.time_left;
                    context->json(result.to_json(), qb::http::Status::CREATED);

                    // Async: fetch bidder username then broadcast WS event
                    // (fire-and-forget; response already sent above)
                    _db->execute("select_user_by_id", {bidder_id},
                        [this, lot, lot_id, amount, bidder_id](qb::pg::transaction &, qb::pg::resultset &&user_res) {
                            models::LotEvent event;
                            event.action = "bid";
                            event.lot_id = lot_id;
                            event.new_price = amount;
                            event.bidder = user_res.empty()
                                ? std::to_string(bidder_id)
                                : user_res[0]["username"].as<std::string>();
                            event.time_left = lot.time_left;
                            event.timestamp = std::time(nullptr);
                            broadcast_lot_event(event);
                        },
                        [this, lot_id, amount, bidder_id](qb::pg::error::db_error const &) {
                            // Username lookup failed — broadcast with numeric ID
                            models::LotEvent event;
                            event.action = "bid";
                            event.lot_id = lot_id;
                            event.new_price = amount;
                            event.bidder = std::to_string(bidder_id);
                            event.time_left = 0;
                            event.timestamp = std::time(nullptr);
                            broadcast_lot_event(event);
                        }
                    );
                },
                [context](qb::pg::error::db_error const &err) {
                    context->internal_server_error(err.what());
                }
            );
        },
        [context](qb::pg::error::db_error const &err) {
            qb::io::cerr() << "[place_bid] insert_bid error: " << err.what() << "\n";
            context->json({{"error", "Bid failed - lot may have ended"}}, qb::http::Status::CONFLICT);
        }
    );
}

void AuctionManager::handle_list_users(ctx_t ctx) {
    auto context = ctx;

    _db->execute("select_all_users", {},
        [context](qb::pg::transaction &, qb::pg::resultset &&res) {
            qb::json::array_t users_arr;
            for (const auto &row : res) {
                models::User user(row);
                users_arr.push_back(user.to_json());
            }
            context->json(qb::json{{"users", users_arr}, {"total", res.size()}});
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

void AuctionManager::handle_get_user(ctx_t ctx) {
    auto user_id = std::stoi(ctx->path_param("id"));
    auto context = ctx;

    _db->execute("select_user_by_id", {user_id},
        [context](qb::pg::transaction &, qb::pg::resultset &&res) {
            if (res.empty()) {
                context->not_found("User not found");
                return;
            }

            models::User user(res[0]);
            context->json(user.to_json());
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

void AuctionManager::handle_get_user_stats(ctx_t ctx) {
    auto user_id = std::stoi(ctx->path_param("id"));
    auto context = ctx;

    // First get user info to have the username
    _db->execute("select_user_by_id", {user_id},
        [this, context, user_id](qb::pg::transaction &, qb::pg::resultset &&user_res) {
            if (user_res.empty()) {
                context->not_found("User not found");
                return;
            }

            models::UserStats stats;
            stats.user_id = user_id;
            stats.username = user_res[0]["username"].as<std::string>().c_str();

            // Then get bid stats
            _db->execute("select_user_stats", {user_id},
                [context, stats](qb::pg::transaction &, qb::pg::resultset &&stats_res) {
                    models::UserStats final_stats = stats;

                    if (!stats_res.empty()) {
                        final_stats.total_bids = stats_res[0]["total_bids"].as<int32_t>();
                        final_stats.active_auctions = stats_res[0]["active_auctions"].as<int32_t>();
                        final_stats.auctions_won = stats_res[0]["auctions_won"].as<int32_t>();
                    }

                    context->json(final_stats.to_json());
                },
                [context](qb::pg::error::db_error const &err) {
                    context->internal_server_error(err.what());
                }
            );
        },
        [context](qb::pg::error::db_error const &err) {
            context->internal_server_error(err.what());
        }
    );
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void AuctionManager::broadcast_lot_event(const models::LotEvent &event) {
    if (!_redis_ready) return;

    _redis.publish("auction:events", event.to_json().dump());
}

void AuctionManager::invalidate_lot_cache(int32_t lot_id) {
    if (!_redis_ready) return;

    _redis.del("lots:active");
    _redis.del("lot:" + std::to_string(lot_id));
    _redis.del("lot:" + std::to_string(lot_id) + ":bids");
}

} // namespace actors
} // namespace auction_house
