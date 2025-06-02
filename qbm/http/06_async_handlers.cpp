/**
 * @file examples/qbm/http/06_async_handlers.cpp
 * @brief HTTP/1.1 asynchronous handlers demonstration using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor with asynchronous request handlers
 * - Using qb::io::async::callback for delayed responses
 * - Non-blocking operations within HTTP handlers
 * - Simulating async database and external API calls
 * - Proper context lifetime management in async operations
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <random>
#include <qb/main.h>
#include <http/http.h>

// Async HTTP Server Actor demonstrating various async patterns
class AsyncServerActor : public qb::Actor, public qb::http::Server<> {
private:
    std::mt19937 _random_gen;
    std::uniform_real_distribution<double> _delay_dist;
    std::uniform_int_distribution<int> _success_dist;
    
public:
    AsyncServerActor() : _random_gen(std::random_device{}()), _delay_dist(0.1, 2.0), _success_dist(1, 10) {}
    
    bool onInit() override {
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
            return false;
        }
        
        return true;
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
        
        // Async handlers demonstrating different patterns
        router().get("/async/simple", [this](auto ctx) {
            handle_async_simple(ctx);
        });
        
        router().get("/async/database", [this](auto ctx) {
            handle_async_database(ctx);
        });
        
        router().get("/async/external-api", [this](auto ctx) {
            handle_async_external_api(ctx);
        });
        
        router().get("/async/multiple-operations", [this](auto ctx) {
            handle_async_multiple_operations(ctx);
        });
        
        router().get("/async/error-prone", [this](auto ctx) {
            handle_async_error_prone(ctx);
        });
        
        router().post("/async/process-data", [this](auto ctx) {
            handle_async_process_data(ctx);
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
    
    void handle_async_simple(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting simple async operation" << std::endl;
        
        // Simulate an async operation with a delay
        double delay = _delay_dist(_random_gen);
        
        qb::io::async::callback([this, ctx, delay]() {
            // Check if the context is still alive
            if (!ctx || !this->is_alive()) {
                std::cout << "[ASYNC] Context or actor no longer alive" << std::endl;
                return;
            }
            
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
            
        }, delay);
    }
    
    void handle_async_database(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting database query simulation" << std::endl;
        
        // Simulate database query with variable delay
        double query_time = _delay_dist(_random_gen);
        
        qb::io::async::callback([this, ctx, query_time]() {
            if (!ctx || !this->is_alive()) return;
            
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
            
        }, query_time);
    }
    
    void handle_async_external_api(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting external API call simulation" << std::endl;
        
        // Simulate external API call with longer delay
        double api_delay = _delay_dist(_random_gen) + 1.0; // 1.1 to 3.0 seconds
        
        qb::io::async::callback([this, ctx, api_delay]() {
            if (!ctx || !this->is_alive()) return;
            
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
            
        }, api_delay);
    }
    
    void handle_async_multiple_operations(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting multiple concurrent operations" << std::endl;
        
        // Store partial results in context
        ctx->set("operations_completed", 0);
        ctx->set("total_operations", 3);
        ctx->set("results", qb::json::object());
        
        // Operation 1: Database query
        double db_delay = _delay_dist(_random_gen);
        qb::io::async::callback([this, ctx, db_delay]() {
            if (!ctx || !this->is_alive()) return;
            
            auto results_opt = ctx->get<qb::json>("results");
            if (!results_opt.has_value()) return;
            
            auto results = results_opt.value();
            results["database"] = {
                {"status", "completed"},
                {"delay", db_delay},
                {"records", 42}
            };
            ctx->set("results", results);
            
            check_multiple_operations_complete(ctx);
            
        }, db_delay);
        
        // Operation 2: Cache lookup
        double cache_delay = _delay_dist(_random_gen) * 0.3; // Faster
        qb::io::async::callback([this, ctx, cache_delay]() {
            if (!ctx || !this->is_alive()) return;
            
            auto results_opt = ctx->get<qb::json>("results");
            if (!results_opt.has_value()) return;
            
            auto results = results_opt.value();
            results["cache"] = {
                {"status", "completed"},
                {"delay", cache_delay},
                {"hit_rate", 0.85}
            };
            ctx->set("results", results);
            
            check_multiple_operations_complete(ctx);
            
        }, cache_delay);
        
        // Operation 3: External service
        double service_delay = _delay_dist(_random_gen) + 0.5;
        qb::io::async::callback([this, ctx, service_delay]() {
            if (!ctx || !this->is_alive()) return;
            
            auto results_opt = ctx->get<qb::json>("results");
            if (!results_opt.has_value()) return;
            
            auto results = results_opt.value();
            results["external_service"] = {
                {"status", "completed"},
                {"delay", service_delay},
                {"data_size", 1024}
            };
            ctx->set("results", results);
            
            check_multiple_operations_complete(ctx);
            
        }, service_delay);
    }
    
    void check_multiple_operations_complete(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto completed_opt = ctx->get<int>("operations_completed");
        auto total_opt = ctx->get<int>("total_operations");
        
        if (!completed_opt.has_value() || !total_opt.has_value()) return;
        
        int completed = completed_opt.value() + 1;
        int total = total_opt.value();
        
        ctx->set("operations_completed", completed);
        
        if (completed >= total) {
            std::cout << "[ASYNC] All " << total << " operations completed" << std::endl;
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            
            auto results_opt = ctx->get<qb::json>("results");
            qb::json results = results_opt.value_or(qb::json::object());
            
            qb::json response = {
                {"type", "multiple_operations"},
                {"message", "All concurrent operations completed successfully"},
                {"operations_count", total},
                {"results", results}
            };
            
            ctx->response().body() = response;
            ctx->complete();
        }
    }
    
    void handle_async_error_prone(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::cout << "[ASYNC] Starting error-prone async operation" << std::endl;
        
        double delay = _delay_dist(_random_gen);
        bool will_succeed = _success_dist(_random_gen) > 3; // 70% success rate
        
        qb::io::async::callback([this, ctx, delay, will_succeed]() {
            if (!ctx || !this->is_alive()) return;
            
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
            
        }, delay);
    }
    
    void handle_async_process_data(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto request_data = ctx->request().body().as<qb::json>();
            
            std::cout << "[ASYNC] Starting data processing operation" << std::endl;
            
            // Simulate processing time based on data size
            int data_size = request_data.value("items", qb::json::array()).size();
            double processing_time = 0.1 + (data_size * 0.05); // Base + per-item time
            
            qb::io::async::callback([this, ctx, request_data, processing_time, data_size]() {
                if (!ctx || !this->is_alive()) return;
                
                std::cout << "[ASYNC] Data processing completed after " << processing_time << "s" << std::endl;
                
                // Simulate processing results
                qb::json processed_items = qb::json::array();
                if (request_data.contains("items")) {
                    for (const auto& item : request_data["items"]) {
                        qb::json processed_item = item;
                        processed_item["processed"] = true;
                        processed_item["processing_time"] = processing_time / data_size;
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
                
            }, processing_time);
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
            ctx->complete();
        }
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