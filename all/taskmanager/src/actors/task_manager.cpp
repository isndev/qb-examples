/**
 * @file src/actors/task_manager.cpp
 * @brief TaskManager lifecycle, HTTP routing, and CRUD route handlers.
 *
 * Implementation breakdown:
 * ┌─ Lifecycle ─────────────────────────────────────────────┐
 * │  constructor · onInit · on(NewConnectionEvent)          │
 * │  on(KillEvent) · disconnected                           │
 * └─────────────────────────────────────────────────────────┘
 * ┌─ Initialisation ────────────────────────────────────────┐
 * │  init_database · prepare_statements                     │
 * │  init_redis · init_websocket_handler · setup_routes     │
 * └─────────────────────────────────────────────────────────┘
 * ┌─ Route handlers ────────────────────────────────────────┐
 * │  handle_health · handle_ws_upgrade                      │
 * │  handle_list_tasks · handle_get_task                    │
 * │  handle_create_task · handle_update_task                │
 * │  handle_delete_task                                     │
 * └─────────────────────────────────────────────────────────┘
 */

#include "actors/task_manager.h"
#include <http/middleware/all.h>
#include <qb/io.h>
#include <filesystem>

namespace taskmanager {
namespace actors {

// ─── Constructor ──────────────────────────────────────────────────────────────

TaskManager::TaskManager(qb::io::uri  pg_uri,
                         qb::io::uri  redis_uri,
                         std::string  static_root)
    : _pg_uri(std::move(pg_uri))
    , _redis_uri(std::move(redis_uri))
    , _static_root(std::move(static_root))
    , _redis(_redis_uri)
    , _ws_handler(*this, _redis_uri) {}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

bool TaskManager::onInit() {
    registerEvent<NewConnectionEvent>(*this);
    registerEvent<qb::KillEvent>(*this);
    registerEvent<qb::SignalEvent>(*this);

    init_database();
    init_redis();
    init_websocket_handler();
    setup_routes();
    router().compile();

    qb::io::cout() << "[TaskManager " << id() << "] ready"
                   << "  db=" << _db_ready
                   << "  redis=" << _redis_ready
                   << "  ws=" << _ws_ready << '\n';

    return _db_ready && _redis_ready;
}

void TaskManager::on(NewConnectionEvent &ev) {
    auto &session = registerSession(std::move(ev.socket));
    qb::io::cout() << "[TaskManager " << id() << "] HTTP session: "
                   << session.id()
                   << "  active=" << session_count() << '\n';
}

void TaskManager::on(qb::SignalEvent const &ev) {
    qb::io::cout() << "[TaskManager " << id() << "] signal " << ev.signum
                   << " – initiating graceful shutdown\n";
    _redis.quit();
    if (_db) _db->disconnect();
    kill();
}

void TaskManager::on(qb::KillEvent const &) {
    qb::io::cout() << "[TaskManager " << id() << "] shutting down\n";
    _redis.quit();
    if (_db) _db->disconnect();
    kill();
}

/**
 * Overrides the base `io_handler::disconnected` to add logging, then
 * **must** call the base to erase the session from the internal map.
 *
 * Failure to call the base leaves the session's shared_ptr alive, keeping
 * the 60-second keep-alive timer active even after the response was sent.
 */
void TaskManager::disconnected(qb::uuid session_id) {
    qb::io::cout() << "[TaskManager " << id() << "] HTTP session "
                   << session_id << " disconnected"
                   << "  remaining=" << (session_count() - 1) << '\n';

    qb::http::use<TaskManager>::io_handler<HttpSession>::disconnected(session_id);
}

// ─── Initialisation ───────────────────────────────────────────────────────────

void TaskManager::init_database() {
    try {
        _db = std::make_unique<qb::pg::tcp::database>(_pg_uri.source());
        if (!_db->connect()) {
            qb::io::cerr() << "[TaskManager " << id() << "] DB connect failed\n";
            return;
        }
        if (!prepare_statements()) {
            qb::io::cerr() << "[TaskManager " << id() << "] prepare statements failed\n";
            return;
        }
        _db_ready = true;
        qb::io::cout() << "[TaskManager " << id() << "] DB connected\n";
    } catch (const std::exception &e) {
        qb::io::cerr() << "[TaskManager " << id() << "] DB init error: "
                       << e.what() << '\n';
    }
}

/**
 * Creates the `tasks` table if it does not exist, then prepares all named
 * SQL statements used by the route handlers.  Executed in a single
 * synchronous transaction during startup (`.await()` is safe here because
 * we are inside `onInit`, not inside an event callback).
 */
bool TaskManager::prepare_statements() {
    if (!_db) return false;

    auto ret =
        _db->begin([](qb::pg::transaction &tr) {
               tr.execute(
                   "CREATE TABLE IF NOT EXISTS tasks ("
                   "  id          SERIAL PRIMARY KEY,"
                   "  title       TEXT NOT NULL,"
                   "  description TEXT,"
                   "  status      TEXT DEFAULT 'pending',"
                   "  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                   "  updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                   ");")
                 .prepare("insert_task",
                          "INSERT INTO tasks (title, description, status)"
                          " VALUES ($1, $2, $3) RETURNING id;",
                          {qb::pg::oid::text, qb::pg::oid::text, qb::pg::oid::text})
                 .prepare("select_all_tasks",
                          "SELECT id, title, description, status,"
                          "       created_at, updated_at"
                          " FROM tasks ORDER BY created_at DESC LIMIT 100;",
                          {})
                 .prepare("select_task_by_id",
                          "SELECT id, title, description, status,"
                          "       created_at, updated_at"
                          " FROM tasks WHERE id = $1;",
                          {qb::pg::oid::int4})
                 .prepare("update_task",
                          // COALESCE(NULLIF($n,''), col) pattern:
                          // if the caller sends an empty string for a field,
                          // the existing DB value is preserved, enabling
                          // partial PUT payloads without overwriting siblings.
                          "UPDATE tasks"
                          " SET title       = COALESCE(NULLIF($1,''), title),"
                          "     description = COALESCE(NULLIF($2,''), description),"
                          "     status      = COALESCE(NULLIF($3,''), status),"
                          "     updated_at  = CURRENT_TIMESTAMP"
                          " WHERE id = $4 RETURNING id;",
                          {qb::pg::oid::text, qb::pg::oid::text,
                           qb::pg::oid::text, qb::pg::oid::int4})
                 .prepare("delete_task",
                          "DELETE FROM tasks WHERE id = $1 RETURNING id;",
                          {qb::pg::oid::int4});
           })
            .await();

    return ret();
}

void TaskManager::init_redis() {
    if (!_redis.connect()) {
        qb::io::cerr() << "[TaskManager " << id() << "] Redis connect failed\n";
        return;
    }
    _redis_ready = true;
    qb::io::cout() << "[TaskManager " << id() << "] Redis connected\n";
}

void TaskManager::init_websocket_handler() {
    if (!_ws_handler.init()) {
        qb::io::cerr() << "[TaskManager " << id() << "] WebSocket handler init failed\n";
        return;
    }
    _ws_ready = true;
    qb::io::cout() << "[TaskManager " << id() << "] WebSocket handler ready\n";
}

/**
 * Attaches middleware (CORS, logging, static files) and registers all REST
 * and WebSocket routes on the router.
 *
 * Middleware execution order (request pipeline):
 *   CORS → Logging → Static files → Route handlers
 *
 * Route hierarchy:
 *   /               → redirect to SPA
 *   /health         → subsystem readiness
 *   /ws             → WebSocket upgrade
 *   /tasks          → RouteGroup (CRUD)
 *     GET    ""     → list all
 *     POST   ""     → create
 *     GET    /:id   → get one
 *     PUT    /:id   → replace / partial update
 *     DELETE /:id   → delete
 */
void TaskManager::setup_routes() {
    // CORS: allow all origins (development mode).
    // In production, replace with a restrictive policy.
    router().use(qb::http::CorsMiddleware<HttpSession>::dev());

    // Access log on every request.
    // Use qb::io::cout() for thread-safe output (multiple actors on different cores).
    router().use(std::make_shared<qb::http::LoggingMiddleware<HttpSession>>(
        [](qb::http::LogLevel level, const std::string &message) {
            const char *lvl = (level == qb::http::LogLevel::Error)   ? "ERROR"
                            : (level == qb::http::LogLevel::Warning)  ? "WARN"
                            : (level == qb::http::LogLevel::Info)     ? "INFO"
                                                                      : "DEBUG";
            qb::io::cout() << "[HTTP:" << lvl << "] " << message << '\n';
        },
        qb::http::LogLevel::Info,
        qb::http::LogLevel::Debug));

    // Static file serving with caching headers and ETag support.
    {
        qb::http::StaticFilesOptions opts(_static_root);
        opts.with_path_prefix_to_strip("/static")
            .with_etags(true)
            .with_last_modified(true)
            .with_cache_control(true, "public, max-age=3600");
        router().use(qb::http::static_files_middleware<HttpSession>(std::move(opts)));
    }

    // Redirect root → SPA entry point.
    router().get("/", [](ctx_t ctx) {
        ctx->redirect("/static/index.html");
    });

    // Health check (used by monitoring + test scripts).
    router().get("/health", [this](ctx_t ctx) { handle_health(ctx); });

    // WebSocket upgrade endpoint.
    router().get("/ws", [this](ctx_t ctx) { handle_ws_upgrade(ctx); });

    // Tasks REST API – grouped under /tasks with RouteGroup for clarity.
    // All child routes inherit the path prefix "/tasks".
    router().group("/tasks")
        ->get ("",     [this](ctx_t ctx) { handle_list_tasks(ctx); })
         .post("",     [this](ctx_t ctx) { handle_create_task(ctx); })
         .get ("/:id", [this](ctx_t ctx) { handle_get_task(ctx); })
         .put ("/:id", [this](ctx_t ctx) { handle_update_task(ctx); })
         .del ("/:id", [this](ctx_t ctx) { handle_delete_task(ctx); });
}

// ─── Route handlers ───────────────────────────────────────────────────────────

/**
 * GET /health
 * Returns current readiness of every subsystem.
 */
void TaskManager::handle_health(ctx_t ctx) {
    ctx->json({
        {"status",     "ok"},
        {"db",         _db_ready},
        {"redis",      _redis_ready},
        {"ws_clients", _ws_handler.client_count()}
    });
}

/**
 * GET /ws
 * Extracts the HTTP session's TCP socket and hands it to WebSocketHandler
 * for the protocol upgrade.  The HTTP context is then suppressed (no HTTP
 * response is sent; the 101 goes directly on the wire).
 */
void TaskManager::handle_ws_upgrade(ctx_t ctx) {
    try {
        auto [transport, ok] = this->extractSession(ctx->session()->id());
        if (!ok) {
            ctx->internal_server_error("Session extraction failed");
            return;
        }

        if (_ws_handler.upgrade_connection(
                std::move(transport), ctx->request(), ctx->response())) {
            ctx->suppress_response();
        } else {
            ctx->bad_request("WebSocket upgrade failed");
        }
    } catch (const std::exception &e) {
        qb::io::cerr() << "[TaskManager] WS upgrade exception: " << e.what() << '\n';
        ctx->internal_server_error("WebSocket upgrade error");
    }
}

/**
 * GET /tasks
 * Lists all tasks.  Tries the Redis cache first (key: `tasks:list`).
 * On a cache miss, queries the DB and caches the result for 60 s.
 * The `X-Cache` response header indicates HIT or MISS.
 */
void TaskManager::handle_list_tasks(ctx_t ctx) {
    if (!_db_ready) {
        ctx->internal_server_error("Database not available");
        return;
    }

    if (_redis_ready) {
        auto cached = _redis.get("tasks:list");
        if (cached.has_value() && !cached->empty()) {
            ctx->response().add_header("X-Cache", "HIT");
            ctx->json(qb::json::parse(*cached));
            return;
        }
    }

    _db->execute(
        "select_all_tasks",
        {},
        [this, ctx](qb::pg::transaction &, qb::pg::resultset &&res) {
            models::TaskList list(res, false);
            auto json = list.to_json();

            if (_redis_ready)
                _redis.setex("tasks:list", 60, json.dump());

            ctx->response().add_header("X-Cache", "MISS");
            ctx->json(json);
        },
        [ctx](qb::pg::error::db_error const &err) {
            ctx->internal_server_error(err.what());
        });
}

/**
 * GET /tasks/:id
 * Returns a single task or 404 if not found.
 */
void TaskManager::handle_get_task(ctx_t ctx) {
    if (!_db_ready) {
        ctx->internal_server_error("Database not available");
        return;
    }

    int32_t task_id;
    try {
        task_id = std::stoi(ctx->path_param("id"));
    } catch (...) {
        ctx->bad_request("Invalid task ID");
        return;
    }

    _db->execute(
        "select_task_by_id",
        {task_id},
        [ctx](qb::pg::transaction &, qb::pg::resultset &&res) {
            if (res.empty()) {
                ctx->not_found("Task not found");
                return;
            }
            models::Task task(res[0]);
            ctx->json(task);
        },
        [ctx](qb::pg::error::db_error const &err) {
            ctx->internal_server_error(err.what());
        });
}

/**
 * POST /tasks
 * Creates a new task.  On success:
 *   - Invalidates the task-list Redis cache.
 *   - Publishes a `created` event to `tasks:events` → WS clients are notified.
 * Returns 201 with `{"id": <new_id>, "status": "created"}`.
 */
void TaskManager::handle_create_task(ctx_t ctx) {
    if (!_db_ready || !_redis_ready) {
        ctx->internal_server_error("Database or Redis not available");
        return;
    }

    models::Task task;
    try {
        auto data      = ctx->request().body().as<qb::json>();
        task.title       = data.at("title").get<std::string>();
        task.description = data.value("description", "");
        task.status      = data.value("status", "pending");
    } catch (...) {
        ctx->bad_request("Invalid JSON – 'title' is required");
        return;
    }

    _db->execute(
        "insert_task",
        {task.title, task.description, task.status},
        [this, ctx, task](qb::pg::transaction &, qb::pg::resultset &&res) {
            const int32_t new_id = res[0][0].as<int32_t>();

            _redis.del("tasks:list");
            _redis.publish("tasks:events",
                           qb::json(models::TaskEvent{"created", new_id, task.title})
                               .dump());

            ctx->json({{"id", new_id}, {"status", "created"}},
                      qb::http::status::CREATED);
        },
        [ctx](qb::pg::error::db_error const &err) {
            ctx->internal_server_error(err.what());
        });
}

/**
 * PUT /tasks/:id
 * Updates title, description, and/or status.  Accepts partial JSON bodies;
 * any missing field keeps the existing value through the COALESCE pattern
 * (the `update_task` prepared statement uses explicit params, so callers
 * are expected to send the full intended state).
 *
 * On success: invalidates cache + publishes `updated` event.
 */
void TaskManager::handle_update_task(ctx_t ctx) {
    if (!_db_ready || !_redis_ready) {
        ctx->internal_server_error("Database or Redis not available");
        return;
    }

    int32_t task_id;
    try {
        task_id = std::stoi(ctx->path_param("id"));
    } catch (...) {
        ctx->bad_request("Invalid task ID");
        return;
    }

    models::Task task;
    try {
        auto data        = ctx->request().body().as<qb::json>();
        // Default to "" so the COALESCE SQL pattern keeps existing DB values
        // for any field omitted in a partial payload.
        task.title       = data.value("title", "");
        task.description = data.value("description", "");
        task.status      = data.value("status", "");
    } catch (...) {
        ctx->bad_request("Invalid JSON");
        return;
    }

    _db->execute(
        "update_task",
        {task.title, task.description, task.status, task_id},
        [this, ctx, task_id](qb::pg::transaction &, qb::pg::resultset &&res) {
            if (res.empty()) {
                ctx->not_found("Task not found");
                return;
            }

            _redis.del("tasks:list");
            _redis.publish("tasks:events",
                           qb::json(models::TaskEvent{"updated", task_id}).dump());

            ctx->json({{"id", task_id}, {"status", "updated"}});
        },
        [ctx](qb::pg::error::db_error const &err) {
            ctx->internal_server_error(err.what());
        });
}

/**
 * DELETE /tasks/:id
 * Deletes the task and publishes a `deleted` event.
 * Returns 200 with `{"id": <id>, "status": "deleted"}` or 404.
 */
void TaskManager::handle_delete_task(ctx_t ctx) {
    if (!_db_ready || !_redis_ready) {
        ctx->internal_server_error("Database or Redis not available");
        return;
    }

    int32_t task_id;
    try {
        task_id = std::stoi(ctx->path_param("id"));
    } catch (...) {
        ctx->bad_request("Invalid task ID");
        return;
    }

    _db->execute(
        "delete_task",
        {task_id},
        [this, ctx, task_id](qb::pg::transaction &, qb::pg::resultset &&res) {
            if (res.empty()) {
                ctx->not_found("Task not found");
                return;
            }

            _redis.del("tasks:list");
            _redis.publish("tasks:events",
                           qb::json(models::TaskEvent{"deleted", task_id}).dump());

            ctx->json({{"id", task_id}, {"status", "deleted"}});
        },
        [ctx](qb::pg::error::db_error const &err) {
            ctx->internal_server_error(err.what());
        });
}

} // namespace actors
} // namespace taskmanager
