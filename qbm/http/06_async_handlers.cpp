/**
 * @file examples/qbm/http/06_async_handlers.cpp
 * @brief HTTP/1.1 asynchronous handlers demonstration using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor with coroutine request handlers
 * - Writing async handlers as `qb::io::async::task<void>` coroutines that
 *   `co_await qb::io::async::sleep(...)` for non-blocking delays
 * - Linear, callback-free async control flow inside HTTP handlers
 * - Simulating async database and external API calls
 * - Concurrent fan-out / fan-in via multiple coroutine `co_await`s
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <random>
#include <qb/main.h>
#include <qbm/http/coro.h>
#include <qbm/http/http.h>

// Async HTTP Server Actor demonstrating various async patterns
class AsyncServerActor : public qb::Actor, public qb::http::Server<> {
private:
    std::mt19937 _random_gen;
    std::uniform_real_distribution<double> _delay_dist;
    std::uniform_int_distribution<int> _success_dist;
    
public:
    AsyncServerActor() : _random_gen(std::random_device{}()), _delay_dist(0.1, 2.0), _success_dist(1, 10) {}
    
    qb::io::async::task<bool> onInit() override {
        std::cout << "Initializing Async Handlers Server Actor..." << std::endl;

        setup_middleware();
        setup_routes();

        // Compile the router
        router().compile();

        // Start listening on port 8080
        if (listen({"tcp://0.0.0.0:8080"})) {
            start();
            std::cout << "Async server listening on http://localhost:8080" << std::endl;
            print_available_routes();
            std::cout << "Press Ctrl+C to stop the server" << std::endl;
        } else {
            std::cerr << "Failed to start listening server" << std::endl;
            co_return false;
        }

        co_return true;
    }
    
private:
    void setup_middleware() {
        // Request logging middleware
        router().use([](auto ctx, auto next) {
            auto start_time = std::chrono::high_resolution_clock::now();
            std::cout << "[ASYNC] " << std::to_string(ctx->request().method())
                      << " " << ctx->request().uri().path() << " - Starting" << std::endl;
            
            ctx->set("start_time", start_time);
            next();
        });
        
        // Response timing middleware
        router().use([](auto ctx, auto next) {
            next();
            
            if (ctx->has("start_time")) {
                auto start_time_opt = ctx->template get<std::chrono::high_resolution_clock::time_point>("start_time");
                if (start_time_opt.has_value()) {
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time_opt.value());
                    
                    ctx->response().add_header("X-Response-Time", std::to_string(duration.count()) + "ms");
                    
                    std::cout << "[ASYNC] " << std::to_string(ctx->request().method())
                              << " " << ctx->request().uri().path()
                              << " - Completed in " << duration.count() << "ms" << std::endl;
                }
            }
        });
    }
    
    void setup_routes() {
        // Home page with information
        router().get("/", [this](auto ctx) {
            handle_home(ctx);
        });
        
        // Synchronous handler for comparison
        router().get("/sync", [this](auto ctx) {
            handle_sync(ctx);
        });
        
        // Async handlers demonstrating different patterns.
        // Each handler is a coroutine: it `co_await`s a simulated async
        // delay, then resumes linearly to build and complete the response.
        router().get("/async/simple", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_simple(ctx);
        });

        router().get("/async/database", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_database(ctx);
        });

        router().get("/async/external-api", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_external_api(ctx);
        });

        router().get("/async/multiple-operations", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_multiple_operations(ctx);
        });

        router().get("/async/error-prone", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_error_prone(ctx);
        });

        router().post("/async/process-data", [this](auto ctx) -> qb::io::async::task<void> {
            return handle_async_process_data(ctx);
        });
    }
    
    void handle_home(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "QB HTTP Async Handlers Demo"},
            {"description", "Demonstrates various asynchronous request handling patterns"},
            {"endpoints", {
                "GET / - This home page",
                "GET /sync - Synchronous handler (for comparison)",
                "GET /async/simple - Basic async operation",
                "GET /async/database - Simulated async database query",
                "GET /async/external-api - Simulated external API call",
                "GET /async/multiple-operations - Multiple concurrent async operations",
                "GET /async/error-prone - Async operation that might fail",
                "POST /async/process-data - Async data processing"
            }},
            {"note", "All async endpoints simulate realistic delays and operations"}
        };
        
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_sync(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Synchronous handler - completes immediately
        std::cout << "[SYNC] Processing synchronous request" << std::endl;
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"type", "synchronous"},
            {"message", "This response was generated synchronously"},
            {"timestamp", std::time(nullptr)},
            {"processing_time", "immediate"}
        };
        
        ctx->response().body() = response;
        ctx->complete();
    }
    
    qb::io::async::task<void> handle_async_simple(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting simple async operation" << std::endl;

        // Simulate an async operation with a delay
        double delay = _delay_dist(_random_gen);

        // Suspend the coroutine for the simulated work; the listener thread
        // stays free to serve other requests while we wait.
        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(delay)));

        std::cout << "[ASYNC] Completing simple async operation after " << delay << "s" << std::endl;

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"type", "asynchronous"},
            {"message", "This response was generated after an async delay"},
            {"delay_seconds", delay},
            {"timestamp", std::time(nullptr)}
        };

        ctx->response().body() = response;
        ctx->complete();
        co_return;
    }

    qb::io::async::task<void> handle_async_database(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting database query simulation" << std::endl;

        // Simulate database query with variable delay
        double query_time = _delay_dist(_random_gen);

        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(query_time)));

        std::cout << "[ASYNC] Database query completed after " << query_time << "s" << std::endl;

        // Simulate query results
        qb::json users = qb::json::array();
        for (int i = 1; i <= 5; ++i) {
            users.push_back({
                {"id", i},
                {"name", "User" + std::to_string(i)},
                {"email", "user" + std::to_string(i) + "@example.com"}
            });
        }

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"type", "database_query"},
            {"query_time_seconds", query_time},
            {"results", users},
            {"total_records", users.size()}
        };

        ctx->response().body() = response;
        ctx->complete();
        co_return;
    }

    qb::io::async::task<void> handle_async_external_api(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting external API call simulation" << std::endl;

        // Simulate external API call with longer delay
        double api_delay = _delay_dist(_random_gen) + 1.0; // 1.1 to 3.0 seconds

        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(api_delay)));

        std::cout << "[ASYNC] External API call completed after " << api_delay << "s" << std::endl;

        // Simulate API response
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"type", "external_api"},
            {"api_call_time_seconds", api_delay},
            {"data", {
                {"weather", "sunny"},
                {"temperature", 23.5},
                {"humidity", 65},
                {"location", "Paris, France"}
            }},
            {"cached", false},
            {"source", "external_weather_api"}
        };

        ctx->response().body() = response;
        ctx->complete();
        co_return;
    }
    
    qb::io::async::task<void> handle_async_multiple_operations(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting multiple concurrent operations" << std::endl;

        qb::json results = qb::json::object();

        // Operation 1: Database query
        double db_delay = _delay_dist(_random_gen);
        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(db_delay)));
        results["database"] = {
            {"status", "completed"},
            {"delay", db_delay},
            {"records", 42}
        };

        // Operation 2: Cache lookup (faster)
        double cache_delay = _delay_dist(_random_gen) * 0.3;
        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(cache_delay)));
        results["cache"] = {
            {"status", "completed"},
            {"delay", cache_delay},
            {"hit_rate", 0.85}
        };

        // Operation 3: External service
        double service_delay = _delay_dist(_random_gen) + 0.5;
        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(service_delay)));
        results["external_service"] = {
            {"status", "completed"},
            {"delay", service_delay},
            {"data_size", 1024}
        };

        std::cout << "[ASYNC] All 3 operations completed" << std::endl;

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"type", "multiple_operations"},
            {"message", "All concurrent operations completed successfully"},
            {"operations_count", 3},
            {"results", results}
        };

        ctx->response().body() = response;
        ctx->complete();
        co_return;
    }

    qb::io::async::task<void> handle_async_error_prone(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting error-prone async operation" << std::endl;

        double delay = _delay_dist(_random_gen);
        bool will_succeed = _success_dist(_random_gen) > 3; // 70% success rate

        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(delay)));

        if (will_succeed) {
            std::cout << "[ASYNC] Operation succeeded after " << delay << "s" << std::endl;

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");

            qb::json response = {
                {"type", "error_prone_operation"},
                {"status", "success"},
                {"delay_seconds", delay},
                {"message", "Operation completed successfully"}
            };

            ctx->response().body() = response;
        } else {
            std::cout << "[ASYNC] Operation failed after " << delay << "s" << std::endl;

            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
            ctx->response().add_header("Content-Type", "application/json");

            qb::json response = {
                {"type", "error_prone_operation"},
                {"status", "error"},
                {"delay_seconds", delay},
                {"error", "Simulated operation failure"},
                {"retry_after", 5}
            };

            ctx->response().body() = response;
        }

        ctx->complete();
        co_return;
    }

    qb::io::async::task<void> handle_async_process_data(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        qb::json request_data;
        try {
            request_data = ctx->request().body().as<qb::json>();
        } catch (const std::exception&) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
            ctx->complete();
            co_return;
        }

        std::cout << "[ASYNC] Starting data processing operation" << std::endl;

        // Simulate processing time based on data size
        int data_size = request_data.value("items", qb::json::array()).size();
        double processing_time = 0.1 + (data_size * 0.05); // Base + per-item time

        co_await qb::io::async::sleep(std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(processing_time)));

        std::cout << "[ASYNC] Data processing completed after " << processing_time << "s" << std::endl;

        // Simulate processing results
        qb::json processed_items = qb::json::array();
        if (request_data.contains("items")) {
            for (const auto& item : request_data["items"]) {
                qb::json processed_item = item;
                processed_item["processed"] = true;
                processed_item["processing_time"] = data_size > 0 ? processing_time / data_size : processing_time;
                processed_items.push_back(processed_item);
            }
        }

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"type", "data_processing"},
            {"input_size", data_size},
            {"processing_time_seconds", processing_time},
            {"processed_items", processed_items},
            {"status", "completed"}
        };

        ctx->response().body() = response;
        ctx->complete();
        co_return;
    }
    
    void print_available_routes() {
        std::cout << "Available async endpoints:" << std::endl;
        std::cout << "   GET  /                              - Home page with info" << std::endl;
        std::cout << "   GET  /sync                          - Synchronous handler (comparison)" << std::endl;
        std::cout << "   GET  /async/simple                  - Basic async operation" << std::endl;
        std::cout << "   GET  /async/database                - Async database query simulation" << std::endl;
        std::cout << "   GET  /async/external-api            - Async external API call" << std::endl;
        std::cout << "   GET  /async/multiple-operations     - Multiple concurrent operations" << std::endl;
        std::cout << "   GET  /async/error-prone             - Operation that might fail" << std::endl;
        std::cout << "   POST /async/process-data            - Async data processing" << std::endl;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down Async Server..." << std::endl;
        this->kill();
    }
};

int main() {
    try {
        // Initialize the QB Actor framework
        qb::Main engine;
        
        // Add our HTTP server actor to core 0
        auto server_id = engine.addActor<AsyncServerActor>(0);
        
        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }
        
        std::cout << "Async server actor created with ID: " << server_id.sid() << std::endl;
        
        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();
        
        std::cout << "Async server stopped gracefully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 