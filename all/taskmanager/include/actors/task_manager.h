/**
 * @file actors/task_manager.h
 * @brief TaskManager – QB Actor, HTTP io_handler, DB/Redis/WS orchestrator (coroutine-first).
 *
 * ## Architecture overview
 * ```
 * ┌──────────────────────────────────────────────────────────┐
 * │                      VirtualCore N                       │
 * │  ┌──────────────────────────────────────────────────┐   │
 * │  │                   TaskManager                    │   │
 * │  │  io_handler<HttpSession>   N HTTP sessions       │   │
 * │  │  WebSocketHandler          M WS sessions + SUB   │   │
 * │  │  qb::pg::tcp::database      co_await SQL          │   │
 * │  │  qb::redis::tcp::client     co_await cache/PUB    │   │
 * │  └──────────────────────────────────────────────────┘   │
 * └──────────────────────────────────────────────────────────┘
 * ```
 *
 * ## Coroutine-first lifecycle (the showcase)
 * `onInit()` is a **coroutine** (`qb::io::async::task<bool>`): it `co_await`s the
 * PostgreSQL connection, schema + prepared statements, the Redis client, and the
 * WebSocket Redis subscriber — *before* the actor activates. While `onInit()` is
 * suspended the actor is **Activating**: inbound `NewConnectionEvent`s are stashed
 * and replayed once it is fully ready, so no HTTP request is ever served against a
 * half-connected backend. This is "discover-before-activate": the actor refuses to
 * go live until its whole machinery is up (`co_return false` aborts creation).
 *
 * Every route handler is a coroutine too (a `task<void>(ctx)` lambda passed
 * directly to the router): they `co_await` the database and Redis directly — no
 * callback pyramids, no readiness flags to check (activation already guaranteed it).
 *
 * ## Session cleanup (CRITICAL)
 * `disconnected(qb::uuid)` overrides the base `io_handler` callback and **must**
 * forward to the base so the session is erased and its `shared_ptr` released.
 * Forgetting it leaks the session for the 60-second keep-alive window.
 */
#pragma once

#include <filesystem>
#include <qbm/http/http.h>
#include <memory>
#include <qbm/pgsql/pgsql.h>
#include <qbm/redis/redis.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include "actors/http_session.h"
#include "actors/websocket_handler.h"
#include "events.h"
#include "models/task.h"

namespace taskmanager {
namespace actors {

/**
 * @brief Main application actor — one fully independent instance per VirtualCore.
 *
 * Serves the REST API + WebSocket upgrade, owns the PostgreSQL, Redis, and
 * WebSocket resources. Shared-nothing: no state crosses cores.
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
    TaskManager(qb::io::uri pg_uri, qb::io::uri redis_uri, std::filesystem::path static_root);

    // ── QB Actor interface ───────────────────────────────────────────────────

    /** Coroutine init: connect DB + Redis + WS subscriber, then activate. */
    qb::io::async::task<bool> onInit() override;

    // ── QB event handlers ────────────────────────────────────────────────────

    /** Accept a new TCP connection dispatched by TcpListener (round-robin). */
    void on(NewConnectionEvent &ev);

    /** Handle OS signal (SIGINT / SIGTERM) for graceful shutdown. */
    void on(qb::SignalEvent const &ev);

    /** Graceful shutdown: tear down the WS subscriber + DB/Redis, then kill. */
    void on(qb::KillEvent const &);

    // ── qb-http io_handler callback ──────────────────────────────────────────

    /** Called when an HttpSession disconnects — logs then forwards to the base. */
    void disconnected(qb::uuid session_id);

private:
    // ── Type alias ───────────────────────────────────────────────────────────

    using ctx_t = std::shared_ptr<qb::http::Context<HttpSession>>;

    // ── Coroutine initialisation helpers ─────────────────────────────────────

    /** Create the `tasks` table and prepare every named SQL statement. */
    qb::io::async::task<bool> prepare_schema();

    /** Register all HTTP routes (coroutine handlers) and attach middleware. */
    void setup_routes();

    /** Graceful resource teardown (idempotent), shared by signal + kill paths. */
    void shutdown_resources();

    // ── Coroutine route handlers ─────────────────────────────────────────────

    qb::io::async::task<void> handle_health(ctx_t ctx);
    qb::io::async::task<void> handle_ws_upgrade(ctx_t ctx);
    qb::io::async::task<void> handle_list_tasks(ctx_t ctx);
    qb::io::async::task<void> handle_get_task(ctx_t ctx);
    qb::io::async::task<void> handle_create_task(ctx_t ctx);
    qb::io::async::task<void> handle_update_task(ctx_t ctx);
    qb::io::async::task<void> handle_delete_task(ctx_t ctx);

    // ── Members ──────────────────────────────────────────────────────────────

    qb::io::uri           _pg_uri;
    qb::io::uri           _redis_uri;
    std::filesystem::path _static_root;

    std::unique_ptr<qb::pg::tcp::database> _db;    ///< PostgreSQL connection.
    qb::redis::tcp::client                 _redis; ///< Redis cache + PUBLISH client.
    WebSocketHandler                       _ws_handler;

    bool _db_ready{false}; ///< reported by /health (true once activated)
    bool _redis_ready{false};
};

} // namespace actors
} // namespace taskmanager
