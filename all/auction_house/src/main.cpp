/**
 * @file main.cpp
 * @brief Auction House - Real-time auction system entry point.
 *
 * Architecture:
 * - Core 0: TcpListener (accepts connections, round-robin dispatch)
 * - Core 1-3: AuctionManager workers (HTTP API + DB + Redis + WS)
 *
 * Database Initialization:
 * - init_db() creates a temporary client and runs init_db.sql via execute_file()
 * - Must happen BEFORE engine.start() so schema is ready for actors
 *
 * Start with: ./auction_house
 * Then open: http://localhost:8080
 */

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <qbm/pgsql/pgsql.h>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/main.h>
#include <qb/system/time.h>

#include "auction_house/actors/auction_manager.h"
#include "auction_house/actors/tcp_listener.h"

/**
 * @brief Bootstrap the database schema by running init_db.sql once, at startup.
 *
 * Pre-engine setup: there is no actor loop yet, so we drive a coroutine to
 * completion synchronously with `qb::io::async::run_sync`. init_db.sql is
 * idempotent (CREATE IF NOT EXISTS + ON CONFLICT seed), so running it on every
 * start is safe. Workers later just connect and prepare statements.
 *
 * @param pg_uri PostgreSQL connection URI.
 * @param init_db_path Path to init_db.sql.
 * @return true on success, false if the file is missing or execution failed.
 */
bool
init_db(const qb::io::uri &pg_uri, const std::filesystem::path &init_db_path) {
    if (!std::filesystem::exists(init_db_path)) {
        qb::io::cerr() << "[Main] init_db.sql not found at: " << init_db_path << "\n";
        return false;
    }

    qb::io::cout() << "[Main] Initializing database from: " << init_db_path << "\n";

    qb::io::async::init(); // this thread needs an event loop to pump run_sync()

    qb::pg::tcp::database db;
    const bool            ok = qb::io::async::run_sync([&db, &pg_uri, &init_db_path]() -> qb::io::async::task<bool> {
        if (!co_await db.connect(pg_uri.source())) {
            qb::io::cerr() << "[Main] DB connect failed for initialization\n";
            co_return false;
        }
        auto r = co_await db.execute_file(init_db_path);
        if (r.ok())
            qb::io::cout() << "[Main] Database initialized successfully\n";
        else
            qb::io::cout() << "[Main] init_db.sql: " << r.error().what() << "\n";
        co_return r.ok();
    }());

    db.disconnect();
    return ok;
}

int
main(int argc, char *argv[]) {
    // Configuration
    constexpr uint16_t PORT          = 8080;
    constexpr uint32_t NUM_WORKERS   = 3;
    constexpr uint32_t LISTENER_CORE = 0;

    // Database configuration (can be overridden via env)
    const char *pg_host = std::getenv("PG_HOST") ? std::getenv("PG_HOST") : "localhost";
    const char *pg_user = std::getenv("PG_USER") ? std::getenv("PG_USER") : "auction_user";
    const char *pg_pass = std::getenv("PG_PASS") ? std::getenv("PG_PASS") : "auction_pass";
    const char *pg_db   = std::getenv("PG_DB") ? std::getenv("PG_DB") : "auction_house";

    const qb::io::uri listen_uri{"tcp://0.0.0.0:" + std::to_string(PORT)};
    const qb::io::uri pg_uri{std::string("tcp://") + pg_user + ":" + pg_pass + "@" + pg_host + ":5432[" + pg_db + "]"};
    const qb::io::uri redis_uri{"tcp://localhost:6379"};

    // Find static resources
    std::filesystem::path static_root = RESOURCES_PATH;
    for (const char *candidate : {RESOURCES_PATH, "./resources/static", "../resources/static", "../../resources/static"}) {
        if (std::filesystem::exists(candidate)) {
            static_root = candidate;
            break;
        }
    }

    // Find init_db.sql
    std::filesystem::path init_db_path = INIT_DB_PATH;
    if (!std::filesystem::exists(init_db_path)) {
        // Try fallback paths
        for (const char *candidate : {INIT_DB_PATH, "./resources/init_db.sql", "../resources/init_db.sql", "../../resources/init_db.sql"}) {
            if (std::filesystem::exists(candidate)) {
                init_db_path = candidate;
                break;
            }
        }
    }

    qb::io::cout() << "╔═══════════════════════════════════════════════════════╗\n";
    qb::io::cout() << "║        AUCTION HOUSE - Real-Time Bidding System       ║\n";
    qb::io::cout() << "╚═══════════════════════════════════════════════════════╝\n";
    qb::io::cout() << "\n";
    qb::io::cout() << "Configuration:\n";
    qb::io::cout() << "  Port:        " << PORT << "\n";
    qb::io::cout() << "  Workers:     " << NUM_WORKERS << "\n";
    qb::io::cout() << "  Database:    " << pg_host << "/" << pg_db << "\n";
    qb::io::cout() << "  Resources:   " << static_root << "\n";
    qb::io::cout() << "\n";

    // ── Database Initialization ────────────────────────────────────────────────
    // CRITICAL: Must happen BEFORE engine.start()
    // This ensures schema exists before actors try to query it

    if (!init_db(pg_uri, init_db_path)) {
        qb::io::cout() << "[Main] Database initialization skipped or failed (schema may already exist)\n";
    }

    qb::io::cout() << "\n";

    // ── Engine Setup ───────────────────────────────────────────────────────────

    qb::Main engine;

    // Register signal handlers for graceful shutdown
    qb::Main::registerSignal(SIGINT);
    qb::Main::registerSignal(SIGTERM);

    // Create worker actors first (need their IDs for listener)
    std::vector<qb::ActorId> worker_ids;
    for (uint32_t i = 0; i < NUM_WORKERS; ++i) {
        // Distribute workers across cores 1, 2, 3
        const uint32_t core = 1 + (i % 3);
        engine.core(core).setLatency(std::chrono::nanoseconds(500'000)); // 500µs for workers

        auto id = engine.addActor<auction_house::actors::AuctionManager>(core, pg_uri, redis_uri, static_root);

        if (!id.is_valid()) {
            qb::io::cerr() << "Failed to create AuctionManager on core " << core << "\n";
            return 1;
        }

        worker_ids.push_back(id);
        qb::io::cout() << "[Main] Worker " << (i + 1) << "/" << NUM_WORKERS << " created on core " << core << " (actor "
                       << static_cast<std::uint32_t>(id) << ")\n";
    }

    // Create TCP listener on dedicated core
    engine.core(LISTENER_CORE).setLatency(qb::duration::zero()); // 0µs for hot loop
    auto listener_id = engine.addActor<auction_house::actors::TcpListener>(LISTENER_CORE, listen_uri, worker_ids);

    if (!listener_id.is_valid()) {
        qb::io::cerr() << "Failed to create TcpListener\n";
        return 1;
    }

    qb::io::cout() << "[Main] TcpListener created on core " << LISTENER_CORE << " (actor " << static_cast<std::uint32_t>(listener_id)
                   << ")\n\n";

    qb::io::cout() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    qb::io::cout() << "Server running at http://localhost:" << PORT << "\n";
    qb::io::cout() << "Press Ctrl+C to stop\n";
    qb::io::cout() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

    // Start engine (blocks until all actors terminate)
    engine.start();
    engine.join();

    // TcpListener::onInit() reports a failed bind and returns false, but that only fails
    // the actor — the process still gets here. Without this check a server that never
    // bound its port exits 0 and a supervisor reads it as a clean shutdown.
    if (engine.hasError()) {
        qb::io::cerr() << "\n[Main] Engine reported an error — see the log above\n";
        return 1;
    }

    qb::io::cout() << "\n[Main] Server stopped. Goodbye!\n";
    return 0;
}
