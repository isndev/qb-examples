/**
 * @file examples/qbm/http/03_basic_routing.cpp
 * @brief Basic HTTP/1.1 routing example with path parameters using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor with advanced routing
 * - Different HTTP methods (GET, POST, PUT, DELETE) in an actor
 * - Path parameters (:id, :name) and wildcard routes (*path)
 * - Query parameters handling
 * - JSON request/response handling within actor context
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <qb/main.h>
#include <http/http.h>

// HTTP Server Actor with advanced routing
class RoutingServerActor : public qb::Actor, public qb::http::Server<> {
private:
    // Simulated in-memory user database
    qb::unordered_map<int, qb::json> _users;
    int _next_user_id = 1;
    
public:
    RoutingServerActor() = default;
    
    bool onInit() override {
        std::cout << "Initializing Routing Server Actor..." << std::endl;
        
        // Initialize some sample data
        initialize_sample_data();
        
        // Set up routes
        setup_routes();
        
        // Compile the router
        router().compile();
        
        // Start listening on port 8080
        if (listen({"tcp://0.0.0.0:8080"})) {
            start();
            std::cout << "Server listening on http://localhost:8080" << std::endl;
            print_available_routes();
            std::cout << "Press Ctrl+C to stop the server" << std::endl;
        } else {
            std::cerr << "Failed to start listening server" << std::endl;
            return false;
        }
        
        return true;
    }
    
private:
    void initialize_sample_data() {
        // Add some sample users
        _users[1] = {{"id", 1}, {"name", "Alice"}, {"email", "alice@example.com"}, {"age", 30}};
        _users[2] = {{"id", 2}, {"name", "Bob"}, {"email", "bob@example.com"}, {"age", 25}};
        _users[3] = {{"id", 3}, {"name", "Charlie"}, {"email", "charlie@example.com"}, {"age", 35}};
        _next_user_id = 4;
    }
    
    void setup_routes() {
        // Root endpoint
        router().get("/", [this](auto ctx) {
            handle_root(ctx);
        });
        
        // API info endpoint
        router().get("/api", [this](auto ctx) {
            handle_api_info(ctx);
        });
        
        // Users collection endpoints
        router().get("/users", [this](auto ctx) {
            handle_get_users(ctx);
        });
        
        router().post("/users", [this](auto ctx) {
            handle_create_user(ctx);
        });
        
        // Individual user endpoints
        router().get("/users/:id", [this](auto ctx) {
            handle_get_user(ctx);
        });
        
        router().put("/users/:id", [this](auto ctx) {
            handle_update_user(ctx);
        });
        
        router().del("/users/:id", [this](auto ctx) {
            handle_delete_user(ctx);
        });
        
        // Parameterized greeting
        router().get("/hello/:name", [this](auto ctx) {
            handle_greeting(ctx);
        });
        
        // Query parameters example
        router().get("/search", [this](auto ctx) {
            handle_search(ctx);
        });
        
        // Wildcard route for file serving simulation
        router().get("/files/*path", [this](auto ctx) {
            handle_file_request(ctx);
        });
    }
    
    void handle_root(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"message", "Welcome to QB HTTP Routing Demo"},
            {"framework", "qb-http"},
            {"version", "1.0"},
            {"endpoints", {
                "GET /api - API information",
                "GET /users - List all users",
                "POST /users - Create a new user",
                "GET /users/:id - Get specific user",
                "PUT /users/:id - Update specific user",
                "DELETE /users/:id - Delete specific user",
                "GET /hello/:name - Personalized greeting",
                "GET /search?q=term - Search example",
                "GET /files/*path - File serving example"
            }}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_api_info(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"api", "QB HTTP Routing Demo API"},
            {"version", "1.0"},
            {"total_users", _users.size()},
            {"next_user_id", _next_user_id}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_get_users(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json users_array = qb::json::array();
        for (const auto& [id, user] : _users) {
            users_array.push_back(user);
        }
        
        qb::json response = {
            {"users", users_array},
            {"count", _users.size()}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_create_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto user_data = ctx->request().body().as<qb::json>();
            
            // Validate required fields
            if (!user_data.contains("name") || !user_data.contains("email")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Missing required fields: name, email"}};
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }
            
            // Create new user
            qb::json new_user = {
                {"id", _next_user_id},
                {"name", user_data["name"]},
                {"email", user_data["email"]},
                {"age", user_data.value("age", 0)}
            };
            
            _users[_next_user_id] = new_user;
            _next_user_id++;
            
            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = new_user;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }
        
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_get_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        int user_id = std::stoi(ctx->path_param("id"));
        
        auto it = _users.find(user_id);
        if (it != _users.end()) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
        }
        
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_update_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        int user_id = std::stoi(ctx->path_param("id"));
        
        auto it = _users.find(user_id);
        if (it == _users.end()) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }
        
        try {
            auto update_data = ctx->request().body().as<qb::json>();
            
            // Update user fields
            if (update_data.contains("name")) it->second["name"] = update_data["name"];
            if (update_data.contains("email")) it->second["email"] = update_data["email"];
            if (update_data.contains("age")) it->second["age"] = update_data["age"];
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }
        
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_delete_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        int user_id = std::stoi(ctx->path_param("id"));
        
        auto it = _users.find(user_id);
        if (it != _users.end()) {
            _users.erase(it);
            ctx->response().status() = qb::http::Status::NO_CONTENT;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
        }
        
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_greeting(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string name = ctx->path_param("name");
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"greeting", "Hello, " + name + "!"},
            {"message", "Welcome to the QB HTTP framework!"},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_search(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto& query_params = ctx->request().queries();
        std::string search_term = ctx->request().query("q");
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"search_term", search_term},
            {"results", qb::json::array()},
            {"total_results", 0},
            {"message", search_term.empty() ? "No search term provided" : "Search functionality would be implemented here"}
        };
        
        // Simulate search in users if term provided
        if (!search_term.empty()) {
            qb::json results = qb::json::array();
            for (const auto& [id, user] : _users) {
                std::string name = user["name"];
                std::string email = user["email"];
                if (name.find(search_term) != std::string::npos || 
                    email.find(search_term) != std::string::npos) {
                    results.push_back(user);
                }
            }
            response["results"] = results;
            response["total_results"] = results.size();
        }
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void handle_file_request(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string file_path = ctx->path_param("path");
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        
        qb::json response = {
            {"requested_file", file_path},
            {"message", "File serving would be implemented here"},
            {"note", "This is a simulation - actual file serving would check file existence and serve content"}
        };
        
        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
    
    void print_available_routes() {
        std::cout << "Available API endpoints:" << std::endl;
        std::cout << "   GET    /              - API overview" << std::endl;
        std::cout << "   GET    /api           - API information" << std::endl;
        std::cout << "   GET    /users         - List all users" << std::endl;
        std::cout << "   POST   /users         - Create new user" << std::endl;
        std::cout << "   GET    /users/:id     - Get user by ID" << std::endl;
        std::cout << "   PUT    /users/:id     - Update user by ID" << std::endl;
        std::cout << "   DELETE /users/:id     - Delete user by ID" << std::endl;
        std::cout << "   GET    /hello/:name   - Personalized greeting" << std::endl;
        std::cout << "   GET    /search?q=term - Search example" << std::endl;
        std::cout << "   GET    /files/*path   - File serving example" << std::endl;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down Routing Server..." << std::endl;
        this->kill();
    }
};

int main() {
    try {
        // Initialize the QB Actor framework
        qb::Main engine;
        
        // Add our HTTP server actor to core 0
        auto server_id = engine.addActor<RoutingServerActor>(0);
        
        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }
        
        std::cout << "Routing server actor created with ID: " << server_id.sid() << std::endl;
        
        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();
        
        std::cout << "Routing server stopped gracefully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 