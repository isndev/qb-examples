# QB TaskManager – Expert Architecture Example (coroutine-first)

A production-grade reference project demonstrating the **QB project convention**
for building multi-actor, full-stack applications on the QB framework — written
**end-to-end with C++20 coroutines**.

Everything that touches the network is a coroutine:

- **`onInit()` is a coroutine** (`qb::io::async::task<bool>`). Each TaskManager
  `co_await`s its PostgreSQL connection, schema + prepared statements, Redis
  client, and WebSocket Redis subscriber *before activating*. While init is
  suspended the actor is **Activating**: inbound connections are stashed and
  replayed once it is fully ready — *discover-before-activate*. A failed backend
  makes `onInit` `co_return false`, so the actor never goes live half-connected.
- **Every route handler is a coroutine** (a `task<void>(ctx)` lambda passed
  directly to the router): it `co_await`s the database and Redis directly — no
  callback pyramids, no readiness flags (activation already guaranteed them).
- **Redis Pub/Sub is a coroutine loop** (`qb::redis::tcp::co_consumer`):
  `while (auto m = co_await receive()) broadcast(...)`.

---

## QB Project Convention

### The golden rule: one class = one file

Every class, actor, session type, and component lives in its own header +
optional `.cpp` file.  This makes the dependency graph explicit and keeps each
translation unit focused.

---

## Directory Structure

```
taskmanager/
├── CMakeLists.txt
├── README.md
│
├── include/                          ← all public headers (no src pollution)
│   ├── events.h                      ← inter-actor QB events (system boundary)
│   ├── models/
│   │   └── task.h                    ← domain models: Task, TaskList, TaskEvent
│   └── actors/
│       ├── tcp_listener.h            ← TcpListener (header-only, trivial actor)
│       ├── http_session.h            ← HttpSession  (thin CRTP wrapper)
│       ├── ws_session.h              ← WsSession    (thin CRTP wrapper)
│       ├── websocket_handler.h       ← WebSocketHandler (inner component)
│       └── task_manager.h            ← TaskManager  (main actor)
│
├── src/
│   ├── main.cpp                      ← engine setup + actor topology
│   └── actors/
│       ├── task_manager.cpp          ← TaskManager lifecycle + route handlers
│       └── websocket_handler.cpp     ← WsSession + WebSocketHandler impl
│
└── resources/
    ├── init_db.sql                   ← optional schema bootstrap
    └── static/
        ├── index.html
        ├── style.css
        └── app.js
```

### File responsibility rules

| File | What belongs there |
|------|--------------------|
| `include/events.h` | QB `struct` events crossing actor boundaries |
| `include/models/*.h` | Plain data structures (serialisable, no QB deps) |
| `include/actors/<name>.h` | Declaration only – types, method signatures, inline trivials |
| `src/actors/<name>.cpp` | All non-trivial method bodies |
| `src/main.cpp` | Engine wiring, zero business logic |

---

## Actor Topology

```
Core 0   TcpListener         ← dedicated accept loop
Core 1   TaskManager #0      ← HTTP io_handler + DB + Redis + WS
Core 2   TaskManager #1
Core 1   TaskManager #2      ← cores reused when workers > cores
```

```
Client TCP connect
  └─ TcpListener::on(accepted_socket)
      └─ push<NewConnectionEvent> → TaskManager (round-robin)
          └─ TaskManager::on(NewConnectionEvent)
              └─ registerSession(socket)       ← HttpSession created
                  └─ HTTP request parsed
                      └─ router().route(ctx)
                          └─ route handler     ← response sent → session closed
```

### WebSocket upgrade flow

```
GET /ws
  └─ TaskManager::handle_ws_upgrade
      └─ extractSession(http_session_id)       ← steals TCP socket from HTTP pool
          └─ WebSocketHandler::upgrade_connection(socket, req, resp)
              └─ registerSession(socket)        ← WsSession created
                  └─ switch_protocol<ws>()     ← WS handshake → 101 response
```

### Real-time event flow (after a mutation)

```
POST /tasks
  └─ DB INSERT → RETURNING id
      └─ redis.del("tasks:list")              ← cache invalidation
          └─ redis.publish("tasks:events", …) ← pub/sub
              └─ co_await consumer.receive()   ← WebSocketHandler::consume_loop()
                  └─ broadcast_to_all(json)
                      └─ WsSession::send_json  × N clients
```

---

## Class Roles

### `TcpListener`  _(header-only)_
- Inherits `qb::Actor` + `qb::io::use<T>::tcp::acceptor`
- Owns the listening socket; accepts connections in a tight loop
- Round-robin dispatch via `push<NewConnectionEvent>(target_id)`
- **Never** touches HTTP; pure connection fan-out

### `HttpSession`  _(declaration only)_
- Thin CRTP wrapper: `qb::http::use<HttpSession>::session<TaskManager>`
- No application logic; the HTTP protocol machinery is in the base class
- Forward-declares `TaskManager` to break the circular dependency

### `TaskManager`  _(actor)_
- Inherits `qb::Actor` + `qb::http::use<T>::io_handler<HttpSession>`
- Owns: `qb::pg::tcp::database`, `qb::redis::tcp::client`, `WebSocketHandler`
- Registers all HTTP routes in `setup_routes()`
- **Must** call `io_handler::disconnected(id)` in its override to prevent leaks

### `WebSocketHandler`  _(inner component, not an actor)_
- Inherits `qb::io::use<T>::tcp::io_handler<WsSession>` (session pool)
- Owns a `qb::redis::tcp::co_consumer` (coroutine Redis SUB)
- `connect_subscriber()` is `co_await`ed from `TaskManager::onInit()`; the actor
  then spawns `consume_loop()` (scoped — cancelled on kill)

### `WsSession`  _(declaration + impl)_
- Thin CRTP wrapper: `qb::io::use<WsSession>::tcp::client<WebSocketHandler>`
- Handles inbound frames via `on(ws_protocol::message&&)`
- Sends data via `send_json(const qb::json&)`

---

## QB Patterns Illustrated

### 1 – CRTP session types with forward declaration

```cpp
// http_session.h
class TaskManager;   // forward declaration – complete type not needed here

class HttpSession : public qb::http::use<HttpSession>::session<TaskManager> {
public:
    explicit HttpSession(TaskManager &mgr) : session(mgr) {}
};
```

The base class stores `TaskManager&` (reference, not value).  A forward
declaration is sufficient; the complete type is only required in `.cpp` files
that call methods on `TaskManager`.

### 2 – Inner component pattern

```cpp
// task_manager.h
class TaskManager : public qb::Actor, public qb::http::use<TaskManager>::io_handler<HttpSession> {
    // ...
    WebSocketHandler _ws_handler;   // inner component, not an actor
};
```

Components that share the same VirtualCore as their owner do **not** need to be
actors.  They are plain C++ objects with QB mixin bases (io_handler, consumer).

### 3 – Session cleanup (CRITICAL)

```cpp
void TaskManager::disconnected(qb::uuid session_id) {
    // Custom logic first ...
    qb::io::cout() << "session " << session_id << " disconnected\n";

    // Then ALWAYS call the base – it erases the session from _sessions
    // and releases its shared_ptr.  Forgetting this causes a 60-second leak.
    qb::http::use<TaskManager>::io_handler<HttpSession>::disconnected(session_id);
}
```

### 4 – Coroutine DB + cache pattern

No callbacks: the handler `co_await`s Redis, then the DB on a miss, then writes
the response. Linear, exception-safe, readable.

```cpp
qb::io::async::task<void> TaskManager::handle_list_tasks(ctx_t ctx) {
    // 1. Redis cache
    auto cached = co_await _redis.get("tasks:list");
    if (cached.ok() && cached.result().has_value() && !cached.result()->empty()) {
        ctx->response().add_header("X-Cache", "HIT");
        ctx->json(qb::json::parse(*cached.result()));
        co_return;
    }

    // 2. Cache miss → DB, then cache for 60 s
    auto res = co_await _db->execute("select_all_tasks", qb::pg::params{});
    if (!res.ok()) { ctx->internal_server_error(res.error().what()); co_return; }

    auto json = models::TaskList(res.result(), false).to_json();
    co_await _redis.setex("tasks:list", 60LL, json.dump());
    ctx->response().add_header("X-Cache", "MISS");
    ctx->json(json);
    co_return;
}
```

### 5 – Event-driven cache invalidation (coroutine)

```cpp
// After any mutation:
co_await _redis.del("tasks:list");                          // invalidate cache
co_await _redis.publish("tasks:events", event_json.dump()); // notify WS clients
```

---

## Build

The rest of `examples/` is mid-port, so this example is wired behind an opt-in
flag that builds **only** taskmanager (and its qbm dependencies):

```bash
# From the workspace root
cmake --preset dev -DQB_BUILD_TASKMANAGER=ON
cmake --build build/presets/dev --target taskmanager -j4
```

The binary is placed in `build/presets/dev/examples/all/taskmanager/taskmanager`.
The static files are copied to `build/presets/dev/bin/resources/static/`.

### Prerequisites

- PostgreSQL running on `localhost:5432`
- Redis running on `localhost:6379`
- Database `taskmanager` with user `test` / password `test`

---

## API

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/health` | Readiness check (DB, Redis, WS count) |
| `GET` | `/tasks` | List tasks (Redis-cached, `X-Cache` header) |
| `POST` | `/tasks` | Create task (`{"title":"…","description":"…","status":"…"}`) |
| `GET` | `/tasks/:id` | Get single task |
| `PUT` | `/tasks/:id` | Update task (partial JSON accepted) |
| `DELETE` | `/tasks/:id` | Delete task |
| `GET` | `/ws` | WebSocket upgrade (101) |
| `GET` | `/static/*` | Static files |
| `GET` | `/` | Redirect → `/static/index.html` |

---

## Testing

A quick functional smoke-test with `curl`:

```bash
# Health
curl -s http://localhost:8080/health | jq

# List tasks
curl -s http://localhost:8080/tasks | jq

# Create
curl -s -X POST http://localhost:8080/tasks \
     -H 'Content-Type: application/json' \
     -d '{"title":"My task","description":"details","status":"pending"}' | jq

# Update
curl -s -X PUT http://localhost:8080/tasks/1 \
     -H 'Content-Type: application/json' \
     -d '{"title":"Updated","status":"in_progress"}' | jq

# Delete
curl -s -X DELETE http://localhost:8080/tasks/1 | jq

# WebSocket (requires websocat)
websocat ws://localhost:8080/ws
```
