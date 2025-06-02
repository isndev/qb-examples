/**
 * @file examples/qbm/http/04_middleware_demo.cpp
 * @brief HTTP/1.1 middleware demonstration using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor with various middleware
 * - Request logging and timing middleware
 * - CORS middleware for cross-origin requests
 * - Basic authentication middleware
 * - Middleware chaining and execution order
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <chrono>
#include <qb/main.h>
#include <http/http.h>

// HTTP Server Actor with middleware demonstration
class MiddlewareServerActor : public qb::Actor, public qb::http::Server<> {
public:
    MiddlewareServerActor() = default;
    
    bool onInit() override {
        std::cout << "Initializing Middleware Demo Server Actor..." << std::endl;
        
        // Set up middleware and routes
        setup_middleware();
        setup_routes();
        
        // Compile the router
        router().compile();
        
        // Start listening on port 8080
        if (listen({"tcp://0.0.0.0:8080"})) {
            start();
            std::cout << "Middleware server listening on http://localhost:8080" << std::endl;
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
        // Global middleware - executed for all requests
        
        // 1. Request logging middleware
        router().use([](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx, std::function<void()> next) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            std::cout << "[LOG] " << ctx->request().method()
                      << " " << ctx->request().uri().path()
                      << " from " << ctx->session()->ip() << std::endl;
            
            // Store start time for timing
            ctx->set("start_time", start_time);
            
            // Continue to next middleware/handler
            next();
        });
        
        // 2. CORS middleware
        router().use([](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx, std::function<void()> next) {
            // Add CORS headers
            ctx->response().add_header("Access-Control-Allow-Origin", "*");
            ctx->response().add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            ctx->response().add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
            
            // Handle preflight OPTIONS requests
            if (ctx->request().method() == qb::http::Method::OPTIONS) {
                ctx->response().status() = qb::http::Status::OK;
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }
            
            next();
        });
        
        // 3. Response timing middleware (executed after request processing)
        router().use([](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx, std::function<void()> next) {
            // Execute the request
            next();
            
            // Add timing information after request is processed
            auto start_time_opt = ctx->template get<std::chrono::high_resolution_clock::time_point>("start_time");
            if (start_time_opt.has_value()) {
                auto start_time = start_time_opt.value();
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                
                ctx->response().add_header("X-Response-Time", std::to_string(duration.count()) + "μs");
                
                std::cout << "[TIMING] " << ctx->request().method()
                          << " " << ctx->request().uri().path()
                          << " completed in " << duration.count() << "μs" << std::endl;
            }
        });
    }
    
    void setup_routes() {
        // Public routes (no authentication required)
        router().get("/", [this](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
            handle_home(ctx);
        });
        
        router().get("/public", [this](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
            handle_public(ctx);
        });
        
        // Protected routes group with authentication middleware
        auto protected_group = router().group("/api");
        
        // Authentication middleware for protected routes
        protected_group->use([](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx, std::function<void()> next) {
            std::string auth_header = ctx->request().header("Authorization");
            
            // C++17 compatible way to check prefix (instead of starts_with)
            const std::string bearer_prefix = "Bearer ";
            if (auth_header.empty() || auth_header.size() < bearer_prefix.size() || 
                auth_header.substr(0, bearer_prefix.size()) != bearer_prefix) {
                ctx->response().status() = qb::http::Status::UNAUTHORIZED;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = R"({"error": "Missing or invalid Authorization header"})";
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }
            
            std::string token = auth_header.substr(7); // Remove "Bearer "
            
            // Simple token validation (in real apps, verify JWT or lookup in database)
            if (token != "secret-token-123") {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = R"({"error": "Invalid token"})";
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }
            
            // Store user information in context
            ctx->set("user_id", std::string("user123"));
            ctx->set("user_name", std::string("John Doe"));
            
            std::cout << "[AUTH] User authenticated: " << token.substr(0, 10) << "..." << std::endl;
            
            next();
        });
        
        // Protected routes
        protected_group->get("/profile", [this](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
            handle_profile(ctx);
        });
        
        protected_group->get("/data", [this](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
            handle_protected_data(ctx);
        });
        
        // Rate limited endpoint with middleware
        auto rate_limited_group = router().group("/limited");
        rate_limited_group->use([](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx, std::function<void()> next) {
            // Simple rate limiting simulation
            static std::chrono::time_point<std::chrono::steady_clock> last_request;
            static int request_count = 0;
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_request).count();
            
            if (elapsed < 1) {
                request_count++;
                if (request_count > 5) {
                    ctx->response().status() = qb::http::Status::TOO_MANY_REQUESTS;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().add_header("Retry-After", "1");
                    ctx->response().body() = R"({"error": "Rate limit exceeded. Try again in 1 second."})";
                    ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                    return;
                }
            } else {
                request_count = 1;
                last_request = now;
            }
            
            next();
        });
        
        rate_limited_group->get("/", [this](std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
            handle_rate_limited(ctx);
        });
    }
    
    void handle_home(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "Welcome to QB HTTP Middleware Demo"},
            {"endpoints", {
                "GET / - This home page",
                "GET /public - Public endpoint",
                "GET /api/profile - Protected endpoint (requires auth)",
                "GET /api/data - Protected data endpoint (requires auth)",
                "GET /limited/ - Rate limited endpoint (5 req/sec)"
            }},
            {"authentication", {
                {"header", "Authorization: Bearer <token>"},
                {"valid_token", "secret-token-123"}
            }}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_public(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "This is a public endpoint"},
            {"accessible", "No authentication required"},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_profile(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Get user information from context (set by auth middleware)
        auto user_id_opt = ctx->get<std::string>("user_id");
        auto user_name_opt = ctx->get<std::string>("user_name");
        
        std::string user_id = user_id_opt.value_or("unknown");
        std::string user_name = user_name_opt.value_or("Unknown User");
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"user_id", user_id},
            {"user_name", user_name},
            {"profile", {
                {"email", "john.doe@example.com"},
                {"role", "user"},
                {"created_at", "2024-01-01T00:00:00Z"}
            }}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_protected_data(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto user_id_opt = ctx->get<std::string>("user_id");
        std::string user_id = user_id_opt.value_or("unknown");
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "Protected data accessed successfully"},
            {"user_id", user_id},
            {"data", {
                {"sensitive_info", "This is protected data"},
                {"balance", 1234.56},
                {"permissions", {"read", "write"}}
            }}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_rate_limited(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "Rate limited endpoint accessed"},
            {"note", "This endpoint allows maximum 5 requests per second"},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void print_available_routes() {
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "   GET  /            - Home page with info" << std::endl;
        std::cout << "   GET  /public      - Public endpoint" << std::endl;
        std::cout << "   GET  /api/profile - Protected user profile (auth required)" << std::endl;
        std::cout << "   GET  /api/data    - Protected data (auth required)" << std::endl;
        std::cout << "   GET  /limited/    - Rate limited endpoint (5 req/sec)" << std::endl;
        std::cout << "\nAuthentication:" << std::endl;
        std::cout << "   Header: Authorization: Bearer secret-token-123" << std::endl;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down Middleware Server..." << std::endl;
        this->kill();
    }
};

int main() {
    try {
        // Initialize the QB Actor framework
        qb::Main engine;
        
        // Add our HTTP server actor to core 0
        auto server_id = engine.addActor<MiddlewareServerActor>(0);
        
        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }
        
        std::cout << "Middleware server actor created with ID: " << server_id.sid() << std::endl;
        
        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();
        
        std::cout << "Middleware server stopped gracefully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 