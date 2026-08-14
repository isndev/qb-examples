/**
 * @file src/actors/task_manager.cpp
 * @brief TaskManager coroutine lifecycle, HTTP routing, and CRUD handlers.
 *
 * Everything that touches the network is a coroutine:
 *   • onInit()        – co_await DB connect + schema + Redis + WS subscriber
 *   • prepare_schema  – co_await CREATE TABLE + prepared statements
 *   • handle_*        – co_await DB queries + Redis cache / pub-sub
 *
 * The actor only activates once all backends are connected (discover-before-
 * activate). Inbound NewConnectionEvents that arrive during init are stashed by
 * the kernel and replayed on activation, so no request is served half-ready.
 */

#include "actors/task_manager.h"
#include <qbm/http/middleware/all.h>
#include <qb/io.h>

namespace taskmanager {
namespace actors {

// ─── Constructor ──────────────────────────────────────────────────────────────

TaskManager::TaskManager(qb::io::uri pg_uri, qb::io::uri redis_uri, std::filesystem::path static_root)
    : _pg_uri(std::move(pg_uri))
    , _redis_uri(std::move(redis_uri))
    , _static_root(std::move(static_root))
    , _redis(_redis_uri)
    , _ws_handler(*this, _redis_uri) {}

// ─── Coroutine lifecycle ────────────────────────────────────────────────────────

qb::io::async::task<bool>
TaskManager::onInit() {
    // Register handlers up-front (before the first co_await) so events that arrive
    // while we are Activating are dispatched correctly once we go live.
    registerEvent<NewConnectionEvent>(*this);
    registerEvent<qb::KillEvent>(*this);
    registerEvent<qb::SignalEvent>(*this);

    // 1. PostgreSQL — connect + schema + prepared statements.
    _db = std::make_unique<qb::pg::tcp::database>();
    if (!co_await _db->connect(_pg_uri.source())) {
        qb::io::cerr() << "[TaskManager " << id() << "] DB connect failed\n";
        co_return false;
    }
    if (!co_await prepare_schema()) {
        qb::io::cerr() << "[TaskManager " << id() << "] schema/prepare failed\n";
        co_return false;
    }
    _db_ready = true;

    // 2. Redis cache + PUBLISH client.
    if (!co_await _redis.connect()) {
        qb::io::cerr() << "[TaskManager " << id() << "] Redis connect failed\n";
        co_return false;
    }
    _redis_ready = true;

    // 3. WebSocket Redis subscriber + actor-scoped consume loop.
    if (!co_await _ws_handler.connect_subscriber())
        co_return false;
    spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
        co_await _ws_handler.consume_loop(); // ends at ~co_consumer, NOT here — see websocket_handler.h
    });

    // 4. HTTP routes — only now that every backend is up.
    setup_routes();
    router().compile();

    qb::io::cout() << "[TaskManager " << id() << "] ready (coroutine init) — db + redis + ws all up\n";
    co_return true;
}

void
TaskManager::on(NewConnectionEvent &ev) {
    auto *session = registerSession(std::move(ev.socket));
    if (!session) {
        qb::io::cerr() << "[TaskManager " << id() << "] HTTP session rejected (limit reached)\n";
        return;
    }
    qb::io::cout() << "[TaskManager " << id() << "] HTTP session " << session->id() << "  active=" << session_count() << '\n';
}

void
TaskManager::on(qb::SignalEvent const &ev) {
    qb::io::cout() << "[TaskManager " << id() << "] signal " << ev.signum << " – graceful shutdown\n";
    shutdown_resources();
    kill();
}

void
TaskManager::on(qb::KillEvent const &) {
    qb::io::cout() << "[TaskManager " << id() << "] shutting down\n";
    shutdown_resources();
    kill();
}

void
TaskManager::shutdown_resources() {
    _ws_handler.shutdown(); // drops the SUB link; does NOT end consume_loop() — see websocket_handler.h
    _redis.disconnect();
    if (_db)
        _db->disconnect();
}

void
TaskManager::disconnected(qb::uuid session_id) {
    qb::io::cout() << "[TaskManager " << id() << "] HTTP session " << session_id << " disconnected  remaining=" << (session_count() - 1)
                   << '\n';
    // ALWAYS forward to the base so the session is erased and its shared_ptr freed.
    qb::http::use<TaskManager>::io_handler<HttpSession>::disconnected(session_id);
}

// ─── Schema + prepared statements (coroutine) ──────────────────────────────────

qb::io::async::task<bool>
TaskManager::prepare_schema() {
    auto created = co_await _db->query("CREATE TABLE IF NOT EXISTS tasks ("
                                       "  id          SERIAL PRIMARY KEY,"
                                       "  title       TEXT NOT NULL,"
                                       "  description TEXT,"
                                       "  status      TEXT DEFAULT 'pending',"
                                       "  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                                       "  updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                                       ");");
    if (!created.ok()) {
        qb::io::cerr() << "[TaskManager " << id() << "] CREATE TABLE failed: " << created.error().what() << '\n';
        co_return false;
    }

    using qb::pg::oid;
    // `prep` is a named local (not a temporary): it outlives every `co_await prep(...)`
    // below, so capturing `this` by value is safe across the inner suspensions.
    const auto prep = [this](std::string_view name, std::string_view sql, qb::pg::type_oid_sequence types) -> qb::io::async::task<bool> {
        auto r = co_await _db->prepare(std::string{name}, std::string{sql}, std::move(types));
        if (!r.ok())
            qb::io::cerr() << "[TaskManager] prepare '" << name << "' failed: " << r.error().what() << '\n';
        co_return r.ok();
    };

    if (!co_await prep("insert_task",
                       "INSERT INTO tasks (title, description, status)"
                       " VALUES ($1, $2, $3) RETURNING id;",
                       qb::pg::type_oid_sequence{oid::text, oid::text, oid::text}))
        co_return false;

    // Cast the TIMESTAMP columns to text so they map cleanly to std::string in the
    // result rows (binary timestamps would otherwise come back empty).
    if (!co_await prep("select_all_tasks",
                       "SELECT id, title, description, status,"
                       "       created_at::text, updated_at::text"
                       " FROM tasks ORDER BY created_at DESC LIMIT 100;",
                       qb::pg::type_oid_sequence{}))
        co_return false;

    if (!co_await prep("select_task_by_id",
                       "SELECT id, title, description, status,"
                       "       created_at::text, updated_at::text"
                       " FROM tasks WHERE id = $1;",
                       qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    // COALESCE(NULLIF($n,''), col): an empty string keeps the existing value, so
    // partial PUT payloads do not clobber omitted fields.
    if (!co_await prep("update_task",
                       "UPDATE tasks SET"
                       "   title       = COALESCE(NULLIF($1,''), title),"
                       "   description = COALESCE(NULLIF($2,''), description),"
                       "   status      = COALESCE(NULLIF($3,''), status),"
                       "   updated_at  = CURRENT_TIMESTAMP"
                       " WHERE id = $4 RETURNING id;",
                       qb::pg::type_oid_sequence{oid::text, oid::text, oid::text, oid::int4}))
        co_return false;

    if (!co_await prep("delete_task", "DELETE FROM tasks WHERE id = $1 RETURNING id;", qb::pg::type_oid_sequence{oid::int4}))
        co_return false;

    co_return true;
}

// ─── Routes ─────────────────────────────────────────────────────────────────────

void
TaskManager::setup_routes() {
    router().use(qb::http::CorsMiddleware<HttpSession>::dev());

    router().use(std::make_shared<qb::http::LoggingMiddleware<HttpSession>>(
        [](qb::http::LogLevel level, const std::string &message) {
            const char *lvl = (level == qb::http::LogLevel::Error)     ? "ERROR"
                              : (level == qb::http::LogLevel::Warning) ? "WARN"
                              : (level == qb::http::LogLevel::Info)    ? "INFO"
                                                                       : "DEBUG";
            qb::io::cout() << "[HTTP:" << lvl << "] " << message << '\n';
        },
        qb::http::LogLevel::Info, qb::http::LogLevel::Debug));

    {
        qb::http::StaticFilesOptions opts(_static_root);
        opts.with_path_prefix_to_strip("/static").with_etags(true).with_last_modified(true).with_cache_control(true, "public, max-age=3600");
        router().use(qb::http::static_files_middleware<HttpSession>(std::move(opts)));
    }

    // Sync handlers (no I/O) stay plain callbacks.
    router().get("/", [](ctx_t ctx) { ctx->redirect("/static/index.html"); });

    // Coroutine member handlers registered directly — no wrapper, no session type.
    router().get("/health", this, &TaskManager::handle_health);
    router().get("/ws", this, &TaskManager::handle_ws_upgrade);

    router()
        .group("/tasks")
        ->get("", this, &TaskManager::handle_list_tasks)
        .post("", this, &TaskManager::handle_create_task)
        .get("/:id", this, &TaskManager::handle_get_task)
        .put("/:id", this, &TaskManager::handle_update_task)
        .del("/:id", this, &TaskManager::handle_delete_task);
}

// ─── Coroutine route handlers ───────────────────────────────────────────────────

qb::io::async::task<void>
TaskManager::handle_health(ctx_t ctx) {
    ctx->json({{"status", "ok"}, {"db", _db_ready}, {"redis", _redis_ready}, {"ws_clients", _ws_handler.client_count()}});
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_ws_upgrade(ctx_t ctx) {
    try {
        auto [transport, ok] = this->extractSession(ctx->session()->id());
        if (!ok) {
            ctx->internal_server_error("Session extraction failed");
            co_return;
        }
        if (_ws_handler.upgrade_connection(std::move(transport), ctx->request(), ctx->response()))
            ctx->suppress_response(); // the 101 went directly on the wire
        else
            ctx->bad_request("WebSocket upgrade failed");
    } catch (const std::exception &e) {
        qb::io::cerr() << "[TaskManager] WS upgrade exception: " << e.what() << '\n';
        ctx->internal_server_error("WebSocket upgrade error");
    }
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_list_tasks(ctx_t ctx) {
    // 1. Redis cache.
    auto cached = co_await _redis.get("tasks:list");
    if (cached.ok() && cached.result().has_value() && !cached.result()->empty()) {
        ctx->response().add_header("X-Cache", "HIT");
        ctx->json(qb::json::parse(*cached.result()));
        co_return;
    }

    // 2. Cache miss → DB, then cache for 60 s.
    auto res = co_await _db->execute("select_all_tasks", qb::pg::params{});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    auto json = models::TaskList(res.result(), false).to_json();
    (void) co_await _redis.setex("tasks:list", 60LL, json.dump()); // best-effort cache

    ctx->response().add_header("X-Cache", "MISS");
    ctx->json(json);
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_get_task(ctx_t ctx) {
    const auto task_id_opt = ctx->path_param<int32_t>("id");
    if (!task_id_opt) {
        ctx->bad_request("Invalid task ID");
        co_return;
    }
    const int32_t task_id = *task_id_opt;

    auto res = co_await _db->execute("select_task_by_id", qb::pg::params{task_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    if (res.result().empty()) {
        ctx->not_found("Task not found");
        co_return;
    }
    ctx->json(models::Task(res.result()[0]));
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_create_task(ctx_t ctx) {
    auto data = ctx->bind<qb::json>(); // parse body once, no throw
    if (!data || !data->contains("title")) {
        ctx->bad_request("Invalid JSON – 'title' is required");
        co_return;
    }
    models::Task task;
    task.title       = data->at("title").get<std::string>();
    task.description = data->value("description", "");
    task.status      = data->value("status", "pending");

    auto res = co_await _db->execute("insert_task", qb::pg::params{task.title, task.description, task.status});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    const int32_t new_id = res.result()[0][0].as<int32_t>();

    (void) co_await _redis.del("tasks:list"); // best-effort cache invalidation
    (void) co_await _redis.publish("tasks:events", qb::json(models::TaskEvent{"created", new_id, task.title}).dump()); // best-effort notify

    ctx->json({{"id", new_id}, {"status", "created"}}, qb::http::status::CREATED);
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_update_task(ctx_t ctx) {
    const auto task_id_opt = ctx->path_param<int32_t>("id");
    if (!task_id_opt) {
        ctx->bad_request("Invalid task ID");
        co_return;
    }
    const int32_t task_id = *task_id_opt;

    models::Task task;
    try {
        auto data = ctx->request().body().as<qb::json>();
        // Default to "" so the COALESCE SQL keeps existing values for omitted fields.
        task.title       = data.value("title", "");
        task.description = data.value("description", "");
        task.status      = data.value("status", "");
    } catch (...) {
        ctx->bad_request("Invalid JSON");
        co_return;
    }

    auto res = co_await _db->execute("update_task", qb::pg::params{task.title, task.description, task.status, task_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    if (res.result().empty()) {
        ctx->not_found("Task not found");
        co_return;
    }

    (void) co_await _redis.del("tasks:list");                                                               // best-effort cache invalidation
    (void) co_await _redis.publish("tasks:events", qb::json(models::TaskEvent{"updated", task_id}).dump()); // best-effort notify

    ctx->json({{"id", task_id}, {"status", "updated"}});
    co_return;
}

qb::io::async::task<void>
TaskManager::handle_delete_task(ctx_t ctx) {
    const auto task_id_opt = ctx->path_param<int32_t>("id");
    if (!task_id_opt) {
        ctx->bad_request("Invalid task ID");
        co_return;
    }
    const int32_t task_id = *task_id_opt;

    auto res = co_await _db->execute("delete_task", qb::pg::params{task_id});
    if (!res.ok()) {
        ctx->internal_server_error(res.error().what());
        co_return;
    }
    if (res.result().empty()) {
        ctx->not_found("Task not found");
        co_return;
    }

    (void) co_await _redis.del("tasks:list");                                                               // best-effort cache invalidation
    (void) co_await _redis.publish("tasks:events", qb::json(models::TaskEvent{"deleted", task_id}).dump()); // best-effort notify

    ctx->json({{"id", task_id}, {"status", "deleted"}});
    co_return;
}

} // namespace actors
} // namespace taskmanager
