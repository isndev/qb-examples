/**
 * @file actors/task_manager.h
 * @brief TaskManager – QB Actor, HTTP io_handler, DB/Redis/WS orchestrator.
 *
 * ## Architecture overview
 * ```
 * ┌──────────────────────────────────────────────────────────┐
 * │                      VirtualCore N                       │
 * │                                                          │
 * │  ┌──────────────────────────────────────────────────┐   │
 * │  │                   TaskManager                    │   │
 * │  │  ┌─────────────────────────┐                    │   │
 * │  │  │  io_handler<HttpSession>│  N HTTP sessions   │   │
 * │  │  └─────────────────────────┘                    │   │
 * │  │  ┌──────────────────────────────────────────┐   │   │
 * │  │  │           WebSocketHandler               │   │   │
 * │  │  │  io_handler<WsSession>  M WS sessions   │   │   │
 * │  │  │  redis::consumer        tasks:events SUB│   │   │
 * │  │  └──────────────────────────────────────────┘   │   │
 * │  │  ┌──────────────────────┐                       │   │
 * │  │  │  qb::pg::tcp::database│  async prepared SQL  │   │
 * │  │  └──────────────────────┘                       │   │
 * │  │  ┌──────────────────────┐                       │   │
 * │  │  │  redis::tcp::client  │  cache + PUBLISH      │   │
 * │  │  └──────────────────────┘                       │   │
 * │  └──────────────────────────────────────────────────┘   │
 * └──────────────────────────────────────────────────────────┘
 * ```
 *
 * ## QB Actor lifecycle
 * 1. `onInit()`   → connect DB + Redis, initialise WebSocketHandler,
 *                   configure HTTP router, compile routes.
 * 2. Events       → `NewConnectionEvent` dispatched by TcpListener.
 * 3. Shutdown     → `qb::KillEvent` triggers graceful resource release.
 *
 * ## Session cleanup (CRITICAL)
 * `TaskManager::disconnected(qb::uuid)` overrides the base `io_handler`
 * callback.  It **must** forward the call to the base implementation so the
 * session is removed from the internal map and its `shared_ptr` is released.
 * Forgetting to do so causes a 60-second memory leak per session (the
 * session's keep-alive timer keeps firing after the response is sent).
 */
#pragma once

#include <qb/actor.h>
#include <http/http.h>
#include <pgsql/pgsql.h>
#include <redis/redis.h>
#include <memory>
#include "events.h"
#include "models/task.h"
#include "actors/http_session.h"
#include "actors/websocket_handler.h"

namespace taskmanager {
namespace actors {

/**
 * @brief Main application actor.
 *
 * Accepts HTTP connections forwarded by TcpListener, serves the REST API,
 * and manages all DB/Redis/WebSocket resources.
 *
 * One instance runs per VirtualCore.  Each instance is fully independent;
 * no state is shared across cores (QB's shared-nothing model).
 */
class TaskManager
    : public qb::Actor
    , public qb::http::use<TaskManager>::io_handler<HttpSession> {
public:
    /**
     * @param pg_uri      PostgreSQL URI, e.g. `tcp://user:pw@host:5432[db]`.
     * @param redis_uri   Redis URI, e.g. `tcp://localhost:6379`.
     * @param static_root Filesystem path served at `/static/`.
     */
    TaskManager(qb::io::uri pg_uri,
                qb::io::uri redis_uri,
                std::string static_root);

    // ── QB Actor interface ───────────────────────────────────────────────────

    bool onInit() final;

    // ── QB event handlers ────────────────────────────────────────────────────

    /** Accept a new TCP connection dispatched by TcpListener (round-robin). */
    void on(NewConnectionEvent &ev);

    /**
     * @brief Handle OS signal (SIGINT / SIGTERM) for graceful shutdown.
     *
     * Triggered when `qb::Main::registerSignal()` is active.  The QB engine
     * broadcasts a `qb::SignalEvent` to every actor on the receiving core.
     */
    void on(qb::SignalEvent const &ev);

    /** Graceful shutdown: close DB + Redis before killing the actor. */
    void on(qb::KillEvent const &);

    // ── qb-http io_handler callback ──────────────────────────────────────────

    /**
     * @brief Called when an HttpSession disconnects.
     *
     * Logs the disconnection then **must** call the base `io_handler`
     * implementation to erase the session from the internal sessions map and
     * release its `shared_ptr`.  Omitting the base call causes indefinite
     * session leaks (the 60-second timeout fires on an already-sent response).
     */
    void disconnected(qb::uuid session_id);

private:
    // ── Type alias ───────────────────────────────────────────────────────────

    using ctx_t = std::shared_ptr<qb::http::Context<HttpSession>>;

    // ── Initialisation helpers ───────────────────────────────────────────────

    /** Connect to PostgreSQL and create schema + prepared statements. */
    void init_database();

    /** Prepare all SQL statements in a single transaction. */
    bool prepare_statements();

    /** Connect to the Redis instance used for caching / publishing. */
    void init_redis();

    /** Initialise WebSocketHandler (Redis subscriber + session pool). */
    void init_websocket_handler();

    /** Register all HTTP routes and attach middleware. */
    void setup_routes();

    // ── Route handlers ───────────────────────────────────────────────────────

    void handle_health    (ctx_t ctx);
    void handle_ws_upgrade(ctx_t ctx);

    // Tasks CRUD
    void handle_list_tasks  (ctx_t ctx);
    void handle_get_task    (ctx_t ctx);
    void handle_create_task (ctx_t ctx);
    void handle_update_task (ctx_t ctx);
    void handle_delete_task (ctx_t ctx);

    // ── Members ──────────────────────────────────────────────────────────────

    qb::io::uri  _pg_uri;
    qb::io::uri  _redis_uri;
    std::string  _static_root;

    std::unique_ptr<qb::pg::tcp::database> _db;      ///< PostgreSQL connection.
    qb::redis::tcp::client                 _redis;   ///< Redis cache client.
    WebSocketHandler                       _ws_handler;

    bool _db_ready{false};
    bool _redis_ready{false};
    bool _ws_ready{false};
};

} // namespace actors
} // namespace taskmanager
