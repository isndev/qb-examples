/**
 * @file examples/qbm/http/02_simple_client.cpp
 * @brief Simple HTTP/1.1 client example using the QB coroutine HTTP client
 *
 * This example demonstrates:
 * - Creating an HTTP client actor using the QB Actor framework
 * - Running the request flow in `spawn()`, an actor-scoped coroutine, rather than
 *   in `onInit()` (see the note on `onInit()` below — this is the load-bearing part)
 * - Making asynchronous HTTP requests with the modern coroutine client
 *   (`co_await qb::http::GET(...)`, `co_await qb::http::POST(...)`)
 * - Handling responses linearly (no callback nesting) via `reply.response`
 * - Using `co_await ctx.sleep(...)` — the ACTOR-SCOPED sleep — for inter-request pacing
 * - Different HTTP methods with proper actor lifecycle
 *
 * @note Requires network access: the endpoints are on the public `httpbin.org`.
 *       For a self-contained coroutine example whose upstreams are its own routes,
 *       see `13_coroutine_handlers.cpp`.
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <qbm/http/coro.h>
#include <qbm/http/http.h>
#include <iostream>
#include <qb/main.h>

// HTTP Client Actor that makes various requests
class HttpClientActor : public qb::Actor {
public:
    HttpClientActor() = default;

    qb::io::async::task<bool>
    onInit() override {
        // Dispatch is by SUBSCRIPTION, not by vtable: `qb::Actor`'s constructor already
        // subscribed the base handlers for these two, and re-registering here REPLACES
        // those entries with ours. Without these two lines the handlers below are dead
        // code — they compile, they are never called.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        std::cout << "Initializing HTTP Client Actor..." << std::endl;

        // The request flow deliberately does NOT run here.
        //
        // 1. `onInit()` is bounded. A suspended `onInit()` must finish within
        //    `qb::VirtualCore::activation_deadline_ns` (5 s by default) or the actor is
        //    cancelled and removed. Three paced network round-trips do not belong in an
        //    initialization budget.
        // 2. This example used to `co_await qb::io::async::sleep(...)` right here, and it
        //    NEVER completed a single request. The free `sleep()` is the standalone qb-io
        //    primitive: it binds to whatever coroutine scheduler is installed on the thread
        //    at the moment it suspends, and when `onInit()` first runs there is none yet —
        //    the core drives that frame directly, before the listener's scheduler exists.
        //    The awaiter therefore latched onto a thread-local fallback scheduler that the
        //    VirtualCore then replaced, so the timer fired into a queue nobody drains.
        //    Anything that goes through the actor (`spawn()`, `context().sleep()`,
        //    `qb::ask`) resolves the listener's scheduler first and is immune.
        // 3. `spawn()` runs the flow on this actor's own cancellation scope: it is
        //    cancelled when the actor is killed, and it is not part of the activation window.
        //
        // The lambda captures NOTHING — see `run_requests`. `ScopedCoroContext` is the only
        // thing that may legally be used after a `co_await`; `this` may not.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> { co_await run_requests(ctx); });

        co_return true;
    }

private:
    // Every step below is `static`: an actor coroutine may be resumed after the actor has
    // been destroyed, so it must not touch actor members past a suspension point. Keeping
    // the flow's state in coroutine locals makes that impossible by construction rather
    // than by discipline.
    static qb::io::async::task<void>
    run_requests(qb::ScopedCoroContext ctx) {
        std::cout << "Starting HTTP requests..." << std::endl;
        std::cout << "=========================" << std::endl;

        int request_count = 0;

        // Example 1: Simple GET request
        co_await make_get_request(++request_count);

        // Example 2: POST request with JSON body.
        // `ctx.sleep()` is the actor-scoped sleep: it wakes immediately (throwing
        // `qb::io::async::cancelled_error`) if the actor is killed while it is pending.
        // The free `qb::io::async::sleep()` would run to term regardless — inside an
        // actor, reach for the context's.
        co_await ctx.sleep(std::chrono::seconds(1));
        co_await make_post_request(++request_count);

        // Example 3: Request with custom headers
        co_await ctx.sleep(std::chrono::seconds(1));
        co_await make_headers_request(++request_count);

        // Finish the demo
        std::cout << "\nHTTP Client demo completed!" << std::endl;
        std::cout << "Total requests made: " << request_count << std::endl;
        std::cout << "Shutting down client actor..." << std::endl;
        qb::Main::stop();
        co_return;
    }

    static qb::io::async::task<void>
    make_get_request(int request_id) {
        std::cout << "Making GET request to httpbin.org..." << std::endl;

        // Create request
        qb::http::Request request(qb::io::uri("http://httpbin.org/get?param1=value1&param2=value2"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Accept", "application/json");
        request.add_header("X-Request-ID", std::to_string(request_id));

        // Await the coroutine client: the call suspends until the reply is
        // ready, then resumes linearly with the response in hand.
        auto reply = co_await qb::http::GET(std::move(request), std::chrono::seconds(10));

        if (reply.response.status() == qb::http::Status::OK) {
            std::cout << "GET Response received:" << std::endl;
            std::cout << "   Status: " << reply.response.status().code() << " " << reply.response.status().str() << std::endl;
            std::cout << "   Content-Type: " << reply.response.header("Content-Type") << std::endl;
            std::cout << "   Body size: " << reply.response.body().size() << " bytes" << std::endl;
            if (reply.response.body().size() < 500) {
                auto body_str = reply.response.body().as<std::string>();
                std::cout << "   Body: " << body_str.substr(0, 200) << "..." << std::endl;
            }
        } else {
            std::cout << "GET request failed with status: " << reply.response.status().code() << std::endl;
        }
        co_return;
    }

    static qb::io::async::task<void>
    make_post_request(int request_id) {
        std::cout << "\nMaking POST request to httpbin.org..." << std::endl;

        // Create request with JSON body
        qb::http::Request request(qb::http::Method::POST, qb::io::uri("http://httpbin.org/post"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Content-Type", "application/json");
        request.add_header("Accept", "application/json");

        // JSON body
        qb::json json_data = {
            {"message", "Hello from QB HTTP Client!"}, {"framework", "qb-http"}, {"timestamp", std::time(nullptr)}, {"request_id", request_id}
        };
        request.body() = json_data;

        auto reply = co_await qb::http::POST(std::move(request), std::chrono::seconds(10));

        if (reply.response.status() == qb::http::Status::OK) {
            std::cout << "POST Response received:" << std::endl;
            std::cout << "   Status: " << reply.response.status().code() << " " << reply.response.status().str() << std::endl;
            std::cout << "   Content-Type: " << reply.response.header("Content-Type") << std::endl;
            std::cout << "   Body size: " << reply.response.body().size() << " bytes" << std::endl;
        } else {
            std::cout << "POST request failed with status: " << reply.response.status().code() << std::endl;
        }
        co_return;
    }

    static qb::io::async::task<void>
    make_headers_request(int request_id) {
        std::cout << "\nMaking request to test custom headers..." << std::endl;

        qb::http::Request request(qb::io::uri("http://httpbin.org/headers"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("X-Custom-Header", "QB-Framework-Test");
        request.add_header("X-Request-ID", std::to_string(request_id));

        auto reply = co_await qb::http::GET(std::move(request), std::chrono::seconds(10));

        if (reply.response.status() == qb::http::Status::OK) {
            std::cout << "Headers Response received:" << std::endl;
            std::cout << "   Status: " << reply.response.status().code() << std::endl;
            std::cout << "   Server echoed our custom headers!" << std::endl;
        } else {
            std::cout << "Headers request failed with status: " << reply.response.status().code() << std::endl;
        }
        co_return;
    }

public:
    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    void
    on(const qb::SignalEvent &event) noexcept {
        std::cout << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(const qb::KillEvent &) noexcept {
        std::cout << "HTTP Client Actor shutting down..." << std::endl;
        this->kill();
    }
};

int
main() {
    try {
        std::cout << "QB HTTP Client Example" << std::endl;
        std::cout << "======================" << std::endl;

        // Initialize the QB Actor framework
        qb::Main engine;

        // Add our HTTP client actor to core 0
        auto client_id = engine.addActor<HttpClientActor>(0);

        if (!client_id.is_valid()) {
            std::cerr << "Failed to create client actor" << std::endl;
            return 1;
        }

        std::cout << "Client actor created with ID: " << client_id.sid() << std::endl;

        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();

        // An actor whose init failed leaves the engine in an error state; report it in
        // the exit code so a supervisor does not read a failed run as a clean shutdown.
        if (engine.hasError()) {
            std::cerr << "Engine reported an error" << std::endl;
            return 1;
        }

        std::cout << "Client demo finished" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
