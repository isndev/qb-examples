/**
 * @file examples/qbm/http/13_coroutine_handlers.cpp
 * @brief Coroutine HTTP route handlers — the modern, linear async style.
 *
 * Demonstrates the coroutine routing API: a route handler may return
 * `qb::io::async::task<void>` and use `co_await` for asynchronous work, instead
 * of callbacks. The router auto-detects coroutine handlers (no wrapper needed).
 *
 * Routes:
 * - GET /delay/:ms   — coroutine handler that `co_await`s an async sleep, then responds.
 * - GET /hello       — plain (synchronous) handler; serves as an upstream target.
 * - GET /proxy       — coroutine handler that `co_await qb::http::GET(...)` an upstream and relays it.
 * - GET /aggregate   — coroutine handler that fetches two upstreams IN PARALLEL via `when_all`.
 * - GET /member      — coroutine handler bound to a member function (`this, &Server::handler`).
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <chrono>
#include <iostream>
#include <string>

#include <qbm/http/coro.h> // co_await qb::http::GET (coroutine client)
#include <qbm/http/http.h>
#include <qb/io/async/coroutine.h> // when_all, sleep
#include <qb/main.h>

class CoroutineServer
    : public qb::Actor
    , public qb::http::Server<> {
public:
    using Session = qb::http::DefaultSession;
    using Context = qb::http::Context<Session>;

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        std::cout << "Initializing Coroutine-Handlers HTTP Server..." << std::endl;

        // 1. Coroutine handler: do asynchronous work (co_await) then respond — linear, no callbacks.
        router().get("/delay/:ms", [](auto ctx) -> qb::io::async::task<void> {
            const auto ms = ctx->template path_param_or<int>("ms", 100);
            co_await qb::io::async::sleep(std::chrono::milliseconds(ms));
            ctx->json(qb::json{{"slept_ms", ms}, {"handler", "coroutine"}});
            co_return;
        });

        // Plain synchronous upstream target for /proxy and /aggregate to call.
        router().get("/hello", [](auto ctx) { ctx->json(qb::json{{"message", "hello from upstream"}}); });

        // 2. Coroutine handler: await an upstream request and relay it (linear proxy).
        router().get("/proxy", [](auto ctx) -> qb::io::async::task<void> {
            auto reply               = co_await qb::http::GET(qb::http::Request{{"http://localhost:8080/hello"}});
            ctx->response().status() = reply.response.status();
            ctx->response().body()   = reply.response.body();
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            co_return;
        });

        // 3. Coroutine handler: fetch two upstreams IN PARALLEL with when_all, then combine.
        router().get("/aggregate", [](auto ctx) -> qb::io::async::task<void> {
            auto fetch = [](std::string path) -> qb::io::async::task<qb::http::async::Reply> {
                co_return co_await qb::http::GET(qb::http::Request{{"http://localhost:8080" + path}});
            };
            auto [a, b] = co_await qb::io::async::when_all(fetch("/hello"), fetch("/delay/50"));
            ctx->json(qb::json{{"hello_status", a.response.status().code()}, {"delay_status", b.response.status().code()}});
            co_return;
        });

        // 4. Coroutine handler bound to a member function.
        router().get("/member", this, &CoroutineServer::handle_member);

        router().compile();
        if (!listen({"tcp://0.0.0.0:8080"})) {
            std::cerr << "Failed to start listening server" << std::endl;
            co_return false;
        }
        start();
        std::cout << "Coroutine server on http://localhost:8080" << std::endl;
        std::cout << "   GET /delay/:ms  /hello  /proxy  /aggregate  /member" << std::endl;
        co_return true;
    }

    // Member coroutine handler (sync or coroutine members both work via the unified verb API).
    qb::io::async::task<void>
    handle_member(std::shared_ptr<Context> ctx) {
        co_await qb::io::async::sleep(std::chrono::milliseconds(10));
        ctx->text("member coroutine handler");
        co_return;
    }

    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    void
    on(const qb::SignalEvent &event) noexcept {
        std::cout << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(const qb::KillEvent &) noexcept {
        this->kill();
    }
};

int
main() {
    try {
        qb::Main engine;
        auto     server_id = engine.addActor<CoroutineServer>(0);
        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }
        engine.start();
        engine.join();

        // A failed onInit() (bind refused) removes the actor but does not make the
        // process fail. Report it, so a supervisor cannot read a server that never
        // bound its port as a clean shutdown.
        if (engine.hasError()) {
            std::cerr << "Engine reported an error" << std::endl;
            return 1;
        }
    } catch (const std::exception &e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
