/**
 * @file examples/07-applications/01-taskmanager/src/main.cpp
 * @tier 07-applications
 * @teaches The whole stack in one program: an acceptor core at zero latency dispatching to a
 *          pool of workers that each own an HTTP router, a WebSocket pool, a PostgreSQL
 *          connection and a Redis cache — and a main() that reports what the engine did.
 * @demonstrates qb::Main, qb::Main::registerSignal, engine.core, setLatency,
 *               qb::duration::zero, addActor<T>, qb::ActorId, engine.start, engine.join,
 *               engine.hasError, qb::io::uri, qb::io::cout, qb::io::cerr
 * @prerequisites 05-services/01-tcp-chat, 06-modules/http/09-coroutine-handlers
 * @expect "[Main] Listening on :"
 * @expect "[Main] Shutdown complete"
 * @brief Entry point – creates the QB engine, actor pool, and starts the loop.
 *
 * ## Actor topology
 * ```
 * Core 0:  TcpListener  (accept loop, round-robin dispatch)
 * Core 1:  TaskManager  #0
 * Core 2:  TaskManager  #1
 * Core 1:  TaskManager  #2   ← cores are reused if more workers than cores
 * ```
 *
 * ## Connection flow
 * ```
 * Client TCP connect
 *   └─ TcpListener::on(accepted_socket_type)
 *       └─ push<NewConnectionEvent> → TaskManager (round-robin)
 *           └─ TaskManager::on(NewConnectionEvent)
 *               └─ registerSession(socket)  → HttpSession created
 *                   └─ HTTP request parsed  → router().route(...)
 *                       └─ route handler    → response sent → session closed
 * ```
 */

#include <chrono>
#include <filesystem>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/system/time.h>

#include "actors/task_manager.h"
#include "actors/tcp_listener.h"

using namespace taskmanager;

int
main() {
    qb::io::cout() << R"(
╔══════════════════════════════════════════════════════╗
║         QB TASKMANAGER  –  Expert Architecture       ║
║                                                      ║
║  TcpListener  →  TaskManager (HTTP + WS + DB + Redis)║
╚══════════════════════════════════════════════════════╝
)" << '\n';

    // ── Configuration ─────────────────────────────────────────────────────────

    constexpr uint16_t PORT              = 8080;
    constexpr uint32_t NUM_TASK_MANAGERS = 3;
    constexpr uint32_t LISTENER_CORE     = 0;

    const qb::io::uri listen_uri{"tcp://0.0.0.0:" + std::to_string(PORT)};
    const qb::io::uri pg_uri{"tcp://test:test@localhost:5432[taskmanager]"};
    const qb::io::uri redis_uri{"tcp://localhost:6379"};

    // Resolve static file root: prefer the compile-time path, fall back to
    // relative paths so the binary works when run from the build directory.
    std::filesystem::path static_root = RESOURCES_PATH;
    for (const char *candidate : {RESOURCES_PATH, "./resources/static", "../resources/static", "../../resources/static"}) {
        if (std::filesystem::exists(candidate)) {
            static_root = candidate;
            break;
        }
    }
    std::filesystem::create_directories(static_root);

    // ── Signal handling ───────────────────────────────────────────────────────
    //
    // Registers OS-level signals with the QB engine.  When a signal fires,
    // every VirtualCore broadcasts a `qb::SignalEvent` to all actors on that
    // core.  Actors override `on(const qb::SignalEvent &)` to react gracefully.
    qb::Main::registerSignal(SIGINT);
    qb::Main::registerSignal(SIGTERM);

    // ── Engine setup ──────────────────────────────────────────────────────────

    qb::Main engine;

    // Core 0 (TcpListener): zero latency = hot loop for minimal accept latency.
    engine.core(LISTENER_CORE).setLatency(qb::duration::zero());

    // Create the TaskManager pool distributed across cores 1–4.
    // Per-core latency is set here (not in a separate loop) so that
    // engine.core(n) is only called for cores that actually receive actors.
    // Calling engine.core(n) registers that VirtualCore with the QB engine;
    // a registered core with zero actors causes [Main] Init Failed.
    std::vector<qb::ActorId> worker_ids;
    worker_ids.reserve(NUM_TASK_MANAGERS);

    for (uint32_t i = 0; i < NUM_TASK_MANAGERS; ++i) {
        const uint32_t core = 1 + (i % 4);
        // 500 µs latency – balances CPU usage vs. response time.
        // setLatency is idempotent; safe to call multiple times on the same core.
        engine.core(core).setLatency(std::chrono::nanoseconds(500'000));
        auto id = engine.addActor<actors::TaskManager>(core, pg_uri, redis_uri, static_root);
        worker_ids.push_back(id);
        qb::io::cout() << "[Main] TaskManager[" << i << "] on core " << core << "  (id=" << id << ")\n";
    }

    // TcpListener on core 0 – dedicated accept loop.
    engine.addActor<actors::TcpListener>(LISTENER_CORE, listen_uri, worker_ids);

    qb::io::cout() << '\n'
                   << "[Main] Listening on :" << PORT << '\n'
                   << "[Main] Static root:  " << static_root << '\n'
                   << "[Main] PostgreSQL:   " << pg_uri.source() << '\n'
                   << "[Main] Redis:        " << redis_uri.source() << '\n'
                   << "[Main] Press Ctrl+C to stop\n\n";

    // ── Run ───────────────────────────────────────────────────────────────────

    engine.start();
    engine.join();

    // TcpListener::onInit() reports a failed bind and returns false, but that only fails
    // the actor — the process still gets here. Without this check a server that never
    // bound its port exits 0 and a supervisor reads it as a clean shutdown.
    if (engine.hasError()) {
        qb::io::cerr() << "[Main] Engine reported an error — see the log above\n";
        return 1;
    }

    qb::io::cout() << "[Main] Shutdown complete\n";
    return 0;
}
