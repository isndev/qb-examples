/**
 * @file examples/qbm/http/01_hello_world_server.cpp
 * @brief Simple HTTP/1.1 "Hello World" server example using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor using the QB Actor framework
 * - Defining simple GET routes within an actor
 * - Proper QB Actor lifecycle management
 * - Starting the server and handling requests asynchronously
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <qb/main.h>
#include <http/http.h>

// Define our HTTP server actor
class HelloWorldServer : public qb::Actor
                       , public qb::http::Server<> {
public:
    HelloWorldServer() = default;
    
    bool onInit() override {
        std::cout << "Initializing Hello World HTTP Server Actor..." << std::endl;
        
        // Set up routes
        router().get("/", [](auto ctx) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "text/plain");
            ctx->response().body() = "Hello, World!\nWelcome to QB HTTP Framework!";
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
        });
        
        router().get("/hello", [](auto ctx) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = R"({"message": "Hello from QB!", "framework": "qb-http", "version": "1.0"})";
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
        });
        
        // Compile the router
        router().compile();
        
        // Start listening on port 8080
        if (listen({"tcp://0.0.0.0:8080"})) {
            start();
            std::cout << "Server listening on http://localhost:8080" << std::endl;
            std::cout << "Available routes:" << std::endl;
            std::cout << "   GET  /       - Hello World message" << std::endl;
            std::cout << "   GET  /hello  - JSON greeting" << std::endl;
            std::cout << "Press Ctrl+C to stop the server" << std::endl;
        } else {
            std::cerr << "Failed to start listening server" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down Hello World Server..." << std::endl;
        // Clean shutdown
        this->kill();
    }
};

int main() {
    try {
        // Initialize the QB Actor framework
        qb::Main engine;
        
        // Add our HTTP server actor to core 0
        auto server_id = engine.addActor<HelloWorldServer>(0);
        
        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }
        
        std::cout << "Server actor created with ID: " << server_id.sid() << std::endl;
        
        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();
        
        std::cout << "Server stopped gracefully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 