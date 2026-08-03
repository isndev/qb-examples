/**
 * @file examples/qbm/http/02_simple_client.cpp
 * @brief Simple HTTP/1.1 client example using the QB coroutine HTTP client
 *
 * This example demonstrates:
 * - Creating an HTTP client actor using the QB Actor framework
 * - Making asynchronous HTTP requests with the modern coroutine client
 *   (`co_await qb::http::GET(...)`, `co_await qb::http::POST(...)`)
 * - Handling responses linearly (no callback nesting) via `reply.response`
 * - Using `co_await qb::io::async::sleep(...)` for inter-request pacing
 * - Different HTTP methods with proper actor lifecycle
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
private:
    int _request_count = 0;

public:
    HttpClientActor() = default;

    qb::io::async::task<bool>
    onInit() override {
        std::cout << "Initializing HTTP Client Actor..." << std::endl;

        // Wait 1 second before starting, then drive the whole request flow
        // as a single linear coroutine. `onInit` is itself a coroutine, so we
        // can simply `co_await` each step in sequence.
        co_await qb::io::async::sleep(std::chrono::seconds(1));

        if (is_alive()) {
            co_await make_requests();
        }

        co_return true;
    }

private:
    qb::io::async::task<void>
    make_requests() {
        std::cout << "Starting HTTP requests..." << std::endl;
        std::cout << "=========================" << std::endl;

        // Example 1: Simple GET request
        co_await make_get_request();

        // Example 2: POST request with JSON body
        co_await qb::io::async::sleep(std::chrono::seconds(1));
        co_await make_post_request();

        // Example 3: Request with custom headers
        co_await qb::io::async::sleep(std::chrono::seconds(1));
        co_await make_headers_request();

        // Finish the demo
        finish_demo();
        co_return;
    }

    qb::io::async::task<void>
    make_get_request() {
        std::cout << "Making GET request to httpbin.org..." << std::endl;

        // Create request
        qb::http::Request request(qb::io::uri("http://httpbin.org/get?param1=value1&param2=value2"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Accept", "application/json");

        // Await the coroutine client: the call suspends until the reply is
        // ready, then resumes linearly with the response in hand.
        auto reply = co_await qb::http::GET(std::move(request), std::chrono::seconds(10));

        ++_request_count;

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

    qb::io::async::task<void>
    make_post_request() {
        std::cout << "\nMaking POST request to httpbin.org..." << std::endl;

        // Create request with JSON body
        qb::http::Request request(qb::http::Method::POST, qb::io::uri("http://httpbin.org/post"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Content-Type", "application/json");
        request.add_header("Accept", "application/json");

        // JSON body
        qb::json json_data = {
            {"message", "Hello from QB HTTP Client!"},
            {"framework", "qb-http"},
            {"timestamp", std::time(nullptr)},
            {"request_id", _request_count + 1}
        };
        request.body() = json_data;

        auto reply = co_await qb::http::POST(std::move(request), std::chrono::seconds(10));

        ++_request_count;

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

    qb::io::async::task<void>
    make_headers_request() {
        std::cout << "\nMaking request to test custom headers..." << std::endl;

        qb::http::Request request(qb::io::uri("http://httpbin.org/headers"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("X-Custom-Header", "QB-Framework-Test");
        request.add_header("X-Request-ID", std::to_string(_request_count + 1));

        auto reply = co_await qb::http::GET(std::move(request), std::chrono::seconds(10));

        ++_request_count;

        if (reply.response.status() == qb::http::Status::OK) {
            std::cout << "Headers Response received:" << std::endl;
            std::cout << "   Status: " << reply.response.status().code() << std::endl;
            std::cout << "   Server echoed our custom headers!" << std::endl;
        } else {
            std::cout << "Headers request failed with status: " << reply.response.status().code() << std::endl;
        }
        co_return;
    }

    void
    finish_demo() {
        std::cout << "\nHTTP Client demo completed!" << std::endl;
        std::cout << "Total requests made: " << _request_count << std::endl;
        std::cout << "Shutting down client actor..." << std::endl;

        // Schedule shutdown after a brief pause
        qb::io::async::callback(
            [this]() {
                if (this->is_alive()) {
                    qb::Main::stop();
                }
            },
            std::chrono::seconds(2));
    }

    void
    on(const qb::KillEvent &event) noexcept {
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

        std::cout << "Client demo finished" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
